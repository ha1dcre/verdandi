// WeaveGraph: a Weave patch -> the SynthDef of one voice and the SynthDef of the effects.
//
// a patch is text, one entry per line (the script builder writes it, macos/blocks):
//   m <id> <type> <param>=<value> ...      a module of the voice (voice and out are always there)
//   c <id>.<output> <id>.<input>           a cable; an input sums all its cables
//   x <id> <type> <param>=<value> ...      an effect, in the order they are chained
//   m main main input=<0..1> level=<0..1>  the line input into the effects, the main level
// every param becomes a control named <id>_<param> (osc1_tune), set live by Engine_Weave.
// the modules and their params are listed in macos/blocks/weave.json (macos/tests/weave_test.js
// checks that both agree). sclang evaluates operators left to right: everything is bracketed.
WeaveGraph {
	classvar <voiceTypes, <fxTypes, <stepped;
	var <modules, <cables, <fx, <values, <order, <>bufnums;

	*initClass {
		// params that are whole numbers or choices: no smoothing
		stepped = [\wave, \type, \color, \loop, \mode, \voices, \slot];
		voiceTypes = IdentityDictionary.new;
		fxTypes = IdentityDictionary.new;

		// ---- voice modules: { |ins, p, v, graph| ... } -> (output: signal); ins[name] is nil when
		// nothing is plugged in; p.(\name) is the control of a param; v holds the voice signals
		voiceTypes[\voice] = { |ins, p, v| v };
		voiceTypes[\out] = { |ins, p, v|
			var sig = (ins[\in] ? DC.ar(0)) * p.(\level);
			var pan = (p.(\pan) + (ins[\pan] ? 0) + (((v[\random] * 2) - 1) * p.(\spread))).clip2(1);
			(stereo: Pan2.ar(sig, pan))
		};
		voiceTypes[\osc] = { |ins, p, v|
			var semis = p.(\tune) + (p.(\fine) / 100) + ((ins[\fm] ? 0) * p.(\fm));
			var freq = ((ins[\pitch] ? v[\pitch]) * (2 ** (semis / 12))).clip(0.1, 20000);
			var pw = (p.(\pw) + ((ins[\pwm] ? 0) * 0.5)).clip(0.02, 0.98);
			(out: Select.ar(p.(\wave), [SinOsc.ar(freq), LFTri.ar(freq), Saw.ar(freq), Pulse.ar(freq, pw)]) * p.(\level))
		};
		voiceTypes[\supersaw] = { |ins, p, v|
			var freq = ((ins[\pitch] ? v[\pitch]) * (2 ** (p.(\tune) / 12))).clip(0.1, 20000);
			var saws = [-3, -2, -1, 0, 1, 2, 3].collect { |k|
				Saw.ar(freq * (1 + (k * p.(\detune) * 0.012)), if(k == 0) { 0.35 } { 0.2 })
			};
			(out: Mix(saws) * p.(\level))
		};
		voiceTypes[\fmop] = { |ins, p, v|
			var freq = ((ins[\pitch] ? v[\pitch]) * p.(\ratio) * (2 ** (p.(\fine) / 1200))).clip(0.1, 20000);
			(out: SinOsc.ar(freq, ((ins[\pm] ? 0) * p.(\index)).clip2(8pi)) * p.(\level))
		};
		voiceTypes[\noise] = { |ins, p, v|
			(out: Select.ar(p.(\color), [WhiteNoise.ar, PinkNoise.ar, BrownNoise.ar]) * p.(\level))
		};
		voiceTypes[\string] = { |ins, p, v|
			var freq = A2K.kr(ins[\pitch] ? v[\pitch]).clip(20, 20000);
			(out: Pluck.ar(WhiteNoise.ar(0.5), ins[\trig] ? v[\gate], 0.05, freq.reciprocal, p.(\decay), p.(\damp)) * p.(\level))
		};
		voiceTypes[\formant] = { |ins, p, v|
			// the first three formants of a, e, i, o, u, blended between neighbouring vowels
			var table = [[800, 350, 270, 450, 325], [1150, 2000, 2140, 800, 700], [2900, 2800, 2950, 2830, 2700]];
			var vowel = (p.(\vowel) + (A2K.kr(ins[\vowel] ? DC.ar(0)) * 2)).clip(0, 4);
			var src = Saw.ar((ins[\pitch] ? v[\pitch]).clip(0.1, 20000));
			var sig = Mix([1, 0.45, 0.2].collect { |amp, k| BPF.ar(src, LinSelectX.kr(vowel, table[k]), 0.08) * amp });
			(out: sig * 3 * p.(\level))
		};
		voiceTypes[\sample] = { |ins, p, v, graph|
			var bufs = graph.bufnums ? (0..7);
			var buf = Select.kr((p.(\slot).round - 1).clip(0, 7), bufs);
			var frames = BufFrames.kr(buf);
			var start = p.(\start) * frames;
			var len = ((p.(\end) - p.(\start)) * frames).max(1);
			var rate = BufRateScale.kr(buf) * (((ins[\pitch] ? v[\pitch]) / 261.63) ** p.(\track));
			var phase = Sweep.ar(ins[\trig] ? v[\gate], rate * SampleRate.ir);
			var looping = p.(\loop);
			var pos = start + (looping * (phase % len)) + ((1 - looping) * phase.min(len));
			var playing = looping + ((1 - looping) * (phase < len));
			(out: BufRd.ar(1, buf, pos, 0, 4) * playing * p.(\level))
		};
		voiceTypes[\filter] = { |ins, p, v|
			var in = ins[\in] ? DC.ar(0);
			var key = ((ins[\pitch] ? v[\pitch]) / 261.63) ** p.(\track);
			var cut = (p.(\cutoff) * (2 ** ((ins[\mod] ? 0) * p.(\mod))) * key).clip(20, 20000);
			var rq = (1 - (p.(\res) * 0.97)).max(0.03);
			(out: Select.ar(p.(\type), [
				MoogFF.ar(in, cut, p.(\res) * 3.8),
				RHPF.ar(in, cut, rq),
				BPF.ar(in, cut, rq) * (1 + (p.(\res) * 3)),
				BRF.ar(in, cut, rq)]))
		};
		voiceTypes[\vca] = { |ins, p, v| (out: (ins[\in] ? DC.ar(0)) * (ins[\cv] ? 1) * p.(\level)) };
		voiceTypes[\mixer] = { |ins, p, v|
			(out: Mix([1, 2, 3, 4].collect { |i| (ins[("in" ++ i).asSymbol] ? DC.ar(0)) * p.(("l" ++ i).asSymbol) }))
		};
		voiceTypes[\drive] = { |ins, p, v|
			var x = (ins[\in] ? DC.ar(0)) * (p.(\amount) * (2 ** (ins[\mod] ? 0)));
			(out: Select.ar(p.(\type), [x.tanh, x.fold2(1), x.clip2(1)]) * p.(\level))
		};
		voiceTypes[\crush] = { |ins, p, v|
			(out: Latch.ar(ins[\in] ? DC.ar(0), Impulse.ar(p.(\rate))).round(2 ** (1 - p.(\bits))))
		};
		voiceTypes[\comb] = { |ins, p, v|
			var freq = ((ins[\pitch] ? v[\pitch]) * (2 ** (p.(\tune) / 12))).clip(20, 20000);
			(out: CombC.ar(ins[\in] ? DC.ar(0), 0.05, freq.reciprocal, p.(\decay)) * p.(\level))
		};
		voiceTypes[\ring] = { |ins, p, v| (out: (ins[\a] ? DC.ar(0)) * (ins[\b] ? DC.ar(0))) };
		voiceTypes[\slew] = { |ins, p, v| (out: Lag.ar(ins[\in] ? DC.ar(0), p.(\time))) };
		voiceTypes[\scale] = { |ins, p, v| (out: ((ins[\in] ? DC.ar(0)) * p.(\amount)) + p.(\offset)) };
		voiceTypes[\env] = { |ins, p, v|
			var env = EnvGen.ar(Env.adsr(p.(\a), p.(\d), p.(\s), p.(\r), 1, p.(\curve)), ins[\gate] ? v[\gate]);
			(out: env * ((1 - p.(\vel)) + (p.(\vel) * v[\vel])))
		};
		voiceTypes[\lfo] = { |ins, p, v|
			var rate = p.(\rate) * (2 ** (ins[\rate] ? 0));
			(out: Select.ar(p.(\wave), [SinOsc.ar(rate), LFTri.ar(rate), LFSaw.ar(rate),
				LFPulse.ar(rate, 0, 0.5, 2, -1), LFNoise0.ar(rate)]) * p.(\level))
		};
		voiceTypes[\sh] = { |ins, p, v| (out: Latch.ar(ins[\in] ? WhiteNoise.ar, Impulse.ar(p.(\rate)))) };
		voiceTypes[\drift] = { |ins, p, v| (out: LFNoise2.ar(p.(\rate)) * p.(\level)) };

		// ---- effects: { |sig, p| ... } -> sig; sig is stereo (an array of two)
		fxTypes[\fx_filter] = { |sig, p|
			var rq = (1 - (p.(\res) * 0.97)).max(0.03);
			sig.collect { |ch| Select.ar(p.(\type), [MoogFF.ar(ch, p.(\cutoff), p.(\res) * 3.8), RHPF.ar(ch, p.(\cutoff), rq)]) }
		};
		fxTypes[\fx_drive] = { |sig, p|
			(sig * (1 - p.(\mix))) + ((sig * p.(\amount)).tanh * p.(\mix))
		};
		fxTypes[\fx_crush] = { |sig, p|
			var wet = Latch.ar(sig, Impulse.ar(p.(\rate))).round(2 ** (1 - p.(\bits)));
			(sig * (1 - p.(\mix))) + (wet * p.(\mix))
		};
		fxTypes[\fx_tape] = { |sig, p|
			var wow = (SinOsc.kr(0.5 + LFNoise1.kr(0.2, 0.2)) * p.(\wow) * 0.004) + (LFNoise2.kr(1.3) * p.(\wow) * 0.0015);
			var flutter = SinOsc.kr(7.3) * p.(\flutter) * 0.0005;
			var wet = DelayC.ar(sig, 0.05, (0.012 + wow + flutter).max(0.001));
			wet = (wet * (1 + (p.(\drive) * 3))).tanh * (1 - (p.(\drive) * 0.35));
			LPF.ar(wet, 16000 - (p.(\drive) * 8000))
		};
		fxTypes[\fx_chorus] = { |sig, p|
			var wet = [0, 0.5pi].collect { |ph, i|
				DelayC.ar(sig[i], 0.05, SinOsc.kr(p.(\rate), ph, p.(\depth) * 0.004, 0.012))
			};
			(sig * (1 - (p.(\mix) * 0.5))) + (wet * p.(\mix))
		};
		fxTypes[\fx_phaser] = { |sig, p|
			var wet = [0, 0.5pi].collect { |ph, i|
				var t = SinOsc.kr(p.(\rate), ph).range(0.0004, 0.0004 + (p.(\depth) * 0.006));
				AllpassC.ar(AllpassC.ar(sig[i], 0.01, t, 0.02), 0.01, t * 0.7, 0.02)
			};
			((sig + (wet * p.(\mix))) * (1 - (p.(\mix) * 0.3)))
		};
		fxTypes[\fx_pitch] = { |sig, p|
			var wet = PitchShift.ar(sig, 0.2, 2 ** (p.(\semis) / 12), 0.01, 0.02);
			(sig * (1 - (p.(\mix) * 0.5))) + (wet * p.(\mix))
		};
		fxTypes[\fx_grains] = { |sig, p|
			var wet = sig.collect { |ch| GrainIn.ar(1, Dust.kr(p.(\density)), p.(\size), ch) * 1.5 };
			(sig * (1 - (p.(\mix) * 0.5))) + (wet * p.(\mix))
		};
		fxTypes[\fx_delay] = { |sig, p|
			var times = [p.(\time), (p.(\time) * (1 + (p.(\spread) * 0.5))).min(2)];
			// the comb's decay time for the feedback asked for: -60 dB after decay seconds
			var decays = times * (0.001.log / p.(\feedback).max(0.001).log);
			var wet = [0, 1].collect { |i| LPF.ar(CombC.ar(sig[i], 2, times[i], decays[i]), p.(\tone)) };
			sig + (wet * p.(\mix))
		};
		fxTypes[\fx_reverb] = { |sig, p| FreeVerb2.ar(sig[0], sig[1], p.(\mix), p.(\size), p.(\damp)) };
		fxTypes[\fx_comp] = { |sig, p|
			Compander.ar(sig, sig, p.(\threshold), 1, p.(\ratio).reciprocal, 0.005, 0.12) * p.(\gain)
		};
		fxTypes[\fx_width] = { |sig, p|
			var mid = (sig[0] + sig[1]) * 0.5, side = (sig[0] - sig[1]) * 0.5 * p.(\width);
			[mid + side, mid - side]
		};
	}

	// the patch Engine_Weave starts with: a plucked saw
	*defaultText {
		^"m voice voice voices=8 mode=0 glide=0 bend=0\n"
		"m osc1 osc wave=2 tune=0 fine=0 pw=0.5 fm=0 level=1\n"
		"m filter1 filter type=0 cutoff=900 res=0.2 mod=3 track=0.3\n"
		"m env1 env a=0.002 d=0.4 s=0.2 r=0.4 curve=-4 vel=0.5\n"
		"m vca1 vca level=1\n"
		"m out out level=0.5 pan=0 spread=0\n"
		"c osc1.out filter1.in\nc env1.out filter1.mod\nc filter1.out vca1.in\nc env1.out vca1.cv\nc vca1.out out.in\n"
		"m main main input=0 level=1\n"
	}

	*parse { |text| ^super.new.parse(text) }

	parse { |text|
		modules = List.new;
		cables = List.new;
		fx = List.new;
		values = IdentityDictionary.new;
		text.asString.split($\n).do { |line|
			var w = line.replace("\r", "").replace("\t", " ").split($ ).reject { |s| s.isEmpty };
			if(w.size > 0) {
				switch(w[0],
					"m", { modules.add(this.entry(w, voiceTypes, "module")) },
					"x", { fx.add(this.entry(w, fxTypes, "effect")) },
					"c", {
						var a = w[1].split($.), b = w[2].split($.);
						if((w.size < 3) or: { a.size != 2 } or: { b.size != 2 }) {
							Error("weave: a cable needs <id>.<output> <id>.<input>: " ++ line).throw
						};
						cables.add([a[0].asSymbol, a[1].asSymbol, b[0].asSymbol, b[1].asSymbol]);
					},
					{ Error("weave: what is this line? " ++ line).throw }
				);
			}
		};
		// main holds the line input and the level of the effects: its values, not a module of the voice
		(modules ++ fx).do { |m| m.params.keysValuesDo { |k, v| values[(m.id ++ "_" ++ k).asSymbol] = v } };
		modules = modules.reject { |m| m.type == \main };
		[\voice, \out].do { |id|
			if(modules.detect { |m| m.id == id }.isNil) { modules.add((id: id, type: id, params: IdentityDictionary.new)) }
		};
		cables.do { |c|
			[c[0], c[2]].do { |id|
				if(modules.detect { |m| m.id == id }.isNil) { Error("weave: a cable to a module that is not there: " ++ id).throw }
			}
		};
		order = this.sorted;
	}

	entry { |w, types, what|
		var params = IdentityDictionary.new;
		if(w.size < 3) { Error("weave: a " ++ what ++ " needs an id and a type: " ++ w.join(" ")).throw };
		if((w[2] != "main") and: { types[w[2].asSymbol].isNil }) { Error("weave: no " ++ what ++ " called " ++ w[2]).throw };
		w.drop(3).do { |kv|
			var pair = kv.split($=);
			if(pair.size == 2) { params[pair[0].asSymbol] = pair[1].asFloat };
		};
		^(id: w[1].asSymbol, type: w[2].asSymbol, params: params)
	}

	// the modules in an order where everything a module listens to comes before it
	sorted {
		var indeg = IdentityDictionary.new, queue, out = List.new;
		modules.do { |m| indeg[m.id] = 0 };
		cables.do { |c| indeg[c[2]] = indeg[c[2]] + 1 };
		queue = modules.select { |m| indeg[m.id] == 0 }.collect { |m| m.id }.asList;
		while { queue.size > 0 } {
			var id = queue.removeAt(0);
			out.add(id);
			cables.do { |c|
				if(c[0] == id) {
					indeg[c[2]] = indeg[c[2]] - 1;
					if(indeg[c[2]] == 0) { queue.add(c[2]) };
				}
			};
		};
		if(out.size < modules.size) { Error("weave: the cables go round in a loop").throw };
		^out
	}

	module { |id| ^modules.detect { |m| m.id == id } }

	// the control of a param: <id>_<param>, starting at the patch's value, smoothed unless stepped
	control { |id, name|
		var key = (id ++ "_" ++ name).asSymbol;
		var start = values[key] ? 0;
		^if(stepped.includes(name)) { NamedControl.kr(key, start) } { NamedControl.kr(key, start, 0.02) }
	}

	paramsOf { |id|
		var made = IdentityDictionary.new;
		// (dict[key] = value answers the dictionary, not the value)
		^{ |name| made[name] ?? { var c = this.control(id, name); made[name] = c; c } }
	}

	voiceDef { |name|
		^SynthDef(name, {
			var freq = \freq.kr(440), gate = \gate.kr(1), vel = \vel.kr(1), trig = \trig.tr(0);
			var vp = this.paramsOf(\voice);
			var pitch = K2A.ar(Lag.kr(freq, vp.(\glide)) * (2 ** (vp.(\bend) / 12)));
			// the gate counts as open for the first two control periods: a note that ends (or a
			// voice killed) before the synth ran once still opens its envelopes, and so ends
			var held = (gate > 0).max(Line.kr(1, 0, ControlDur.ir * 2) > 0);
			// a retrigger (mono mode) drops the gate for two control periods
			var gateSig = K2A.ar(held * (1 - Trig1.kr(trig, ControlDur.ir * 2)));
			var v = (pitch: pitch, gate: gateSig, vel: K2A.ar(vel), random: K2A.ar(Rand(0, 1)));
			var signals = IdentityDictionary.new, stereo, life, kill = \kill.kr(0), ln;
			order.do { |id|
				var m = this.module(id), ins = IdentityDictionary.new;
				cables.do { |c|
					if(c[2] == id) {
						var src = signals[c[0]] !? { |outs| outs[c[1]] };
						if(src.isNil) { Error("weave: " ++ c[0] ++ " has no output " ++ c[1]).throw };
						ins[c[3]] = if(ins[c[3]].isNil) { src } { ins[c[3]] + src };
					}
				};
				signals[id] = if(id == \voice) { v } { voiceTypes[m.type].value(ins, this.paramsOf(id), v, this) };
			};
			stereo = signals[\out][\stereo];
			// the voice lives on for \life seconds after the gate closes (the longest release),
			// then frees itself; \kill fades it out in 10 ms and frees it (a stolen voice)
			ln = \life.kr(0.5);
			life = EnvGen.kr(Env.new([0, 1, 1, 0], [0, ln * 0.85, (ln * 0.15) + 0.02], \lin, 1), held, doneAction: 2);
			FreeSelf.kr(TDelay.kr(kill > 0.5, 0.03));
			Out.ar(\out.kr(0), stereo * life * Lag.kr(1 - (kill > 0.5), 0.01));
		})
	}

	fxDef { |name|
		^SynthDef(name, {
			var main = this.paramsOf(\main);
			var sig = In.ar(\in.kr(0), 2) + (SoundIn.ar([0, 1]) * main.(\input));
			fx.do { |m| sig = fxTypes[m.type].value(sig, this.paramsOf(m.id)) };
			sig = Limiter.ar(LeakDC.ar(sig * main.(\level)), 0.97, 0.01);
			Out.ar(\out.kr(0), sig);
		})
	}

	// how long a voice may ring after its note ends: the longest release in the patch
	lifeFor { |vals|
		var longest = 0.05;
		modules.do { |m|
			var t = switch(m.type,
				\env, { vals[(m.id ++ "_r").asSymbol] },
				\string, { vals[(m.id ++ "_decay").asSymbol] },
				\comb, { vals[(m.id ++ "_decay").asSymbol] },
				{ nil });
			if(t.notNil) { longest = max(longest, t) };
		};
		^(longest * 1.25) + 0.05
	}
}
