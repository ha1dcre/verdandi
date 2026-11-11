// the sclang half of macos/tests/weave_test.js: builds each patch of a manifest with WeaveGraph,
// lists the controls the catalog promises but the SynthDefs lack, renders the notes offline
// (NRT, no audio device) into <out dir>/<name>.wav, and writes <out dir>/results.json.
//   sclang -l <conf with WeaveGraph.sc> macos/tests/weave_test.scd <manifest.json> <out dir>
(
var argv = thisProcess.argv;
var manifest = File.readAllString(argv[0]).parseJSON;
var outDir = argv[1];
var results = List.new;
var esc = { |s| s.asString.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", " ") };
var str = { |s| if(s.isNil) { "null" } { "\"" ++ esc.(s) ++ "\"" } };
fork {
	manifest.do { |item|
		var name = item["name"], res = (name: name), g, vdef, fdef;
		try {
			g = WeaveGraph.parse(item["text"]);
			g.bufnums = (0..7);
			vdef = g.voiceDef(\weave_voice);
			fdef = g.fxDef(\weave_fx);
		} { |e| res[\error] = e.errorString };
		if(res[\error].isNil) {
			var names = (vdef.allControlNames ++ fdef.allControlNames).collect { |c| c.name.asString };
			// (includes compares identity: strings by ==); voices and mode are the engine's, not a synth's
			res[\missing] = (item["controls"] ? []).reject { |c|
				names.any { |n| n == c } or: { ["voice_voices", "voice_mode"].any { |e| e == c } }
			};
			if(item["notes"].notNil) {
				var cond = Condition.new, path = outDir +/+ name ++ ".wav", score;
				var vals = g.values.asKeyValuePairs, life = g.lifeFor(g.values), dur = item["dur"].asFloat;
				score = [[0.0, [\d_recv, vdef.asBytes]], [0.0, [\d_recv, fdef.asBytes]]]
					++ 8.collect { |i| [0.0, [\b_alloc, i, 1, 1]] }
					++ [[0.0, [\s_new, \weave_fx, 1000, 1, 0, \in, 16, \out, 0] ++ vals]];
				item["notes"].do { |n, i|
					score = score ++ [
						[n[0].asFloat, [\s_new, \weave_voice, 2000 + i, 0, 0, \out, 16, \freq, n[2].asFloat,
							\vel, n[3].asFloat, \gate, 1, \life, life] ++ vals],
						[n[1].asFloat, [\n_set, 2000 + i, \gate, 0]]];
				};
				score = Score(score ++ [[dur, [\c_set, 0, 0]]]);
				score.sort;
				score.recordNRT(nil, path, sampleRate: 48000, headerFormat: "WAV", sampleFormat: "float",
					options: ServerOptions.new.numOutputBusChannels_(2).numInputBusChannels_(2),
					duration: dur, action: { cond.unhang });
				cond.hang;
				res[\wav] = path;
			};
		};
		results.add(res);
	};
	File.use(outDir +/+ "results.json", "w", { |f|
		f.write("[\n" ++ results.collect { |r|
			"{\"name\": " ++ str.(r[\name]) ++ ", \"error\": " ++ str.(r[\error]) ++ ", \"missing\": ["
			++ (r[\missing] ? []).collect { |m| str.(m) }.join(", ") ++ "], \"wav\": " ++ str.(r[\wav]) ++ "}"
		}.join(",\n") ++ "\n]\n");
	});
	0.exit;
};
)
