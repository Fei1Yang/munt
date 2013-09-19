#include <SDL_endian.h>
#include <mt32emu/mt32emu.h>
#include "mixer.h"
#include "control.h"

static class MidiHandler_mt32 : public MidiHandler {
private:
	MixerChannel *chan;
	MT32Emu::Synth *synth;
	bool open, noise;

	class MT32ReportHandler : public MT32Emu::ReportHandler {
	protected:
		virtual void onErrorControlROM() {
			LOG_MSG("MT32: Couldn't open Control ROM file");
		}

		virtual void onErrorPCMROM() {
			LOG_MSG("MT32: Couldn't open PCM ROM file");
		}

		virtual void showLCDMessage(const char *message) {
			LOG_MSG("MT32: LCD-Message: %s", message);
		}

		virtual void printDebug(const char *fmt, va_list list);
	} reportHandler;

	static void makeROMPathName(char pathName[], const char romDir[], const char fileName[], bool addPathSeparator) {
		strcpy(pathName, romDir);
		if (addPathSeparator) {
			strcat(pathName, "/");
		}
		strcat(pathName, fileName);
	}

public:
	MidiHandler_mt32() : open(false), chan(NULL), synth(NULL) {}

	~MidiHandler_mt32() {
		Close();
	}

	const char *GetName(void) {
		return "mt32";
	}

	bool Open(const char *conf) {
		Section_prop *section = static_cast<Section_prop *>(control->GetSection("midi"));
		const char *romDir = section->Get_string("mt32.romdir");
		if (romDir == NULL) romDir = "./"; // Paranoid NULL-check, should never happen
		size_t romDirLen = strlen(romDir);
		bool addPathSeparator = false;
		if (romDirLen < 1) {
			romDir = "./";
		} else if (4080 < romDirLen) {
			LOG_MSG("MT32: mt32.romdir is too long, using the current dir.");
			romDir = "./";
		} else {
			char lastChar = romDir[strlen(romDir) - 1];
			addPathSeparator = lastChar != '/' && lastChar != '\\';
		}

		char pathName[4096];
		MT32Emu::FileStream controlROMFile;
		MT32Emu::FileStream pcmROMFile;

		makeROMPathName(pathName, romDir, "CM32L_CONTROL.ROM", addPathSeparator);
		if (!controlROMFile.open(pathName)) {
			makeROMPathName(pathName, romDir, "MT32_CONTROL.ROM", addPathSeparator);
			if (!controlROMFile.open(pathName)) {
				LOG_MSG("MT32: Control ROM file not found");
				return false;
			}
		}
		makeROMPathName(pathName, romDir, "CM32L_PCM.ROM", addPathSeparator);
		if (!pcmROMFile.open(pathName)) {
			makeROMPathName(pathName, romDir, "MT32_PCM.ROM", addPathSeparator);
			if (!pcmROMFile.open(pathName)) {
				LOG_MSG("MT32: PCM ROM file not found");
				return false;
			}
		}
		const MT32Emu::ROMImage *controlROMImage = MT32Emu::ROMImage::makeROMImage(&controlROMFile);
		const MT32Emu::ROMImage *pcmROMImage = MT32Emu::ROMImage::makeROMImage(&pcmROMFile);
		synth = new MT32Emu::Synth(&reportHandler);
		if (!synth->open(*controlROMImage, *pcmROMImage)) {
			LOG_MSG("MT32: Error initialising emulation");
			return false;
		}
		MT32Emu::ROMImage::freeROMImage(controlROMImage);
		MT32Emu::ROMImage::freeROMImage(pcmROMImage);

		if (strcmp(section->Get_string("mt32.reverb.mode"), "auto") != 0) {
			Bit8u reverbsysex[] = {0x10, 0x00, 0x01, 0x00, 0x05, 0x03};
			reverbsysex[3] = (Bit8u)atoi(section->Get_string("mt32.reverb.mode"));
			reverbsysex[4] = (Bit8u)section->Get_int("mt32.reverb.time");
			reverbsysex[5] = (Bit8u)section->Get_int("mt32.reverb.level");
			synth->writeSysex(16, reverbsysex, 6);
			synth->setReverbOverridden(true);
		} else {
			LOG_MSG("MT32: Using default reverb");
		}

		if (strcmp(section->Get_string("mt32.dac"), "auto") != 0) {
			synth->setDACInputMode((MT32Emu::DACInputMode)atoi(section->Get_string("mt32.dac")));
		}

		synth->setReversedStereoEnabled(strcmp(section->Get_string("mt32.reverse.stereo"), "on") == 0);
		noise = strcmp(section->Get_string("mt32.verbose"), "on") == 0;

		chan = MIXER_AddChannel(mixerCallBack, MT32Emu::SAMPLE_RATE, "MT32");
		chan->Enable(true);

		open = true;
		return true;
	}

	void Close(void) {
		if (!open) return;
		chan->Enable(false);
		MIXER_DelChannel(chan);
		chan = NULL;
		synth->close();
		delete synth;
		synth = NULL;
		open = false;
	}

	void PlayMsg(Bit8u *msg) {
		synth->playMsg(SDL_SwapLE32(*(Bit32u *)msg));
	}

	void PlaySysex(Bit8u *sysex, Bitu len) {
		synth->playSysex(sysex, len);
	}

private:
	static void mixerCallBack(Bitu len);

	void render(Bitu len, Bit16s *buf) {
		synth->render(buf, len);
		chan->AddSamples_s16(len, buf);
	}
} midiHandler_mt32;

void MidiHandler_mt32::MT32ReportHandler::printDebug(const char *fmt, va_list list) {
	if (midiHandler_mt32.noise) {
		char s[1024];
		strcpy(s, "MT32: ");
		vsnprintf(s + 6, 1017, fmt, list);
		LOG_MSG(s);
	}
}

void MidiHandler_mt32::mixerCallBack(Bitu len) {
	midiHandler_mt32.render(len, (Bit16s *)MixTemp);
}
