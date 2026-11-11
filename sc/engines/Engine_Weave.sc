// Engine_Weave: the modular engine of the script builder (macos/blocks).
//
// a patch (WeaveGraph) of modules and cables plays as voices: one synth per note, up to
// voice_voices of them (the oldest is stolen), or one held synth in mono / legato mode; all
// voices go through the chain of effects. every param of the patch is live: set "<id>.<param>".
//
// commands:
//   patch s            build the patch (text, see WeaveGraph); all notes end
//   note_on fff        id, frequency (Hz), velocity (0..1); the id ends it again (a note number)
//   note_off f         id
//   set sf             "<id>.<param>", value (option params: 0-based)
//   all_off i          every note released (the argument is not used)
//   kill i             every voice silenced at once (the argument is not used)
//   load_sample is     slot 1..8, path of a sound file (its first channel), for the sample module
// poll: voices, the number of voices sounding (held or ringing out)
Engine_Weave : CroneEngine {
	var graph, values, ready = false;
	var bus, voiceGroup, fxGroup, fxSynth, buffers;
	var voices, alive, held, mono;

	*new { arg context, doneCallback;
		^super.new(context, doneCallback);
	}

	alloc {
		var s = context.server;
		bus = Bus.audio(s, 2);
		voiceGroup = Group.new(context.xg);
		fxGroup = Group.after(voiceGroup);
		buffers = 8.collect { Buffer.alloc(s, 1, 1) };
		voices = Dictionary.new;
		alive = List.new;
		held = List.new;
		s.sync;
		this.build(WeaveGraph.defaultText);

		this.addCommand("patch", "s", { arg msg; this.build(msg[1].asString) });
		this.addCommand("note_on", "fff", { arg msg; this.noteOn(msg[1], msg[2], msg[3]) });
		this.addCommand("note_off", "f", { arg msg; this.noteOff(msg[1]) });
		this.addCommand("set", "sf", { arg msg; this.setParam(msg[1].asString, msg[2]) });
		this.addCommand("all_off", "i", { this.allOff });
		this.addCommand("kill", "i", { this.killAll });
		this.addCommand("load_sample", "is", { arg msg; this.loadSample(msg[1], msg[2].asString) });
		// how many voices sound (held or ringing out)
		this.addPoll(\voices, { alive.size });
	}

	build { arg text;
		var g, vdef, fdef, s = context.server;
		try {
			g = WeaveGraph.parse(text);
			g.bufnums = buffers.collect { |b| b.bufnum };
			vdef = g.voiceDef(\weave_voice);
			fdef = g.fxDef(\weave_fx);
		} { |error|
			postln("weave: the patch was not built: " ++ error.errorString);
			g = nil;
		};
		if(g.notNil) {
			ready = false;
			fork {
				vdef.send(s);
				fdef.send(s);
				s.sync;
				this.killAll;
				fxSynth !? { fxSynth.free };
				graph = g;
				values = g.values.copy;
				fxSynth = Synth(\weave_fx, [\in, bus.index, \out, context.out_b.index] ++ values.asKeyValuePairs, fxGroup);
				ready = true;
				postln("weave: patch ready (" ++ g.modules.size ++ " modules, " ++ g.fx.size ++ " effects)");
			};
		};
	}

	life { ^graph.lifeFor(values) }
	mode { ^(values[\voice_mode] ? 0).asInteger }

	newVoice { arg hz, vel;
		var syn = Synth(\weave_voice, [\out, bus.index, \freq, hz, \vel, vel, \gate, 1, \life, this.life]
			++ values.asKeyValuePairs, voiceGroup);
		alive.add(syn);
		syn.onFree {
			alive.remove(syn);
			if(mono === syn) { mono = nil };
		};
		^syn
	}

	noteOn { arg id, hz, vel;
		var max;
		if(ready.not) { ^this };
		if(this.mode == 0) {
			voices.removeAt(id) !? { |old| old.set(\gate, 0) };
			// steal: a releasing voice first, else the oldest held one
			max = (values[\voice_voices] ? 8).asInteger.clip(1, 16);
			while { alive.size >= max } {
				var victim = alive.detect { |v| voices.findKeyForValue(v).isNil } ? alive.first;
				alive.remove(victim);
				voices.findKeyForValue(victim) !? { |k| voices.removeAt(k) };
				victim.set(\kill, 1);
			};
			voices[id] = this.newVoice(hz, vel);
		} {
			held.removeAllSuchThat { |n| n[0] == id };
			held.add([id, hz]);
			if(mono.notNil) {
				mono.set(\freq, hz, \vel, vel, \gate, 1);
				// mono restarts the envelopes on every note, legato only after all keys were up
				if(this.mode == 1) { mono.set(\trig, 1) };
			} {
				mono = this.newVoice(hz, vel);
			};
		};
	}

	noteOff { arg id;
		if(this.mode == 0) {
			voices.removeAt(id) !? { |v| v.set(\gate, 0) };
		} {
			held.removeAllSuchThat { |n| n[0] == id };
			mono !? { |m|
				if(held.size > 0) { m.set(\freq, held.last[1]) } { m.set(\gate, 0) };
			};
		};
	}

	allOff {
		voices.do { |v| v.set(\gate, 0) };
		voices.clear;
		held.clear;
		mono !? { |m| m.set(\gate, 0) };
	}

	killAll {
		alive.copy.do { |v| v.set(\kill, 1) };
		voices.clear;
		held.clear;
		mono = nil;
	}

	setParam { arg key, value;
		var name = key.replace(".", "_").asSymbol;
		if(values.isNil) { ^this };
		values[name] = value;
		voiceGroup.set(name, value);
		fxSynth !? { fxSynth.set(name, value) };
		if((name == \voice_mode) and: { value.asInteger == 0 }) {
			mono !? { |m| m.set(\gate, 0) };
			mono = nil;
			held.clear;
		};
		// a longer release lets the voices ring longer
		if(key.endsWith(".r") or: { key.endsWith(".decay") }) { voiceGroup.set(\life, this.life) };
	}

	loadSample { arg slot, path;
		var b = buffers[(slot.asInteger - 1).clip(0, 7)];
		if(File.exists(path).not) { postln("weave: no sound file at " ++ path); ^this };
		b.allocReadChannel(path, channels: [0], completionMessage: { postln("weave: sample " ++ slot ++ " loaded: " ++ path); nil });
	}

	free {
		voiceGroup.free;
		fxGroup.free;
		bus.free;
		buffers.do { |b| b.free };
	}
}
