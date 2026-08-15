#include "../plugin.hpp"
#include <osdialog.h>
#include <fstream>
#include <sstream>
#include <mutex>
#include <algorithm>
#include <cctype>
#include "BTFParser.hpp"
#include "Scale.hpp"
#include "Quantizer.hpp"

using namespace bmbo;

// ============================================================================
// Module
// ============================================================================
//
// The Module owns all DSP state (the Quantizer + currently loaded Scale) and
// all file I/O / parsing. It deliberately does NOT know about osdialog or any
// other GUI concern -- the Widget is responsible for opening the file picker
// and simply hands the resulting path to loadFromPath(). This keeps the DSP
// core testable and reusable independent of the UI toolkit.

struct BmboBTFQuantizer : Module {
	enum ParamId {
		ROOT_TRANSPOSE_PARAM,
		OCTAVE_SHIFT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CV_INPUT,
		ROOT_CV_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		CV_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(STATUS_LIGHT, 2), // 0 = green (loaded ok), 1 = red (parse failed)
		LIGHTS_LEN
	};

	Quantizer quantizer;
	mutable std::mutex quantizerMutex;

	// Status info surfaced on the display / LED. Protected by the same
	// mutex as the quantizer's Scale since they change together.
	std::string statusMessage = "No tuning loaded";
	bool lastLoadSuccess = false;
	bool hasAttemptedLoad = false;

	// Remembers the last successfully-loaded file's raw text so the tuning
	// survives patch save/reload without depending on the file still being
	// present on disk at the original path.
	std::string lastLoadedText;
	std::string lastLoadedPath;

	// UI theme, toggled from the right-click context menu. Purely cosmetic
	// -- has no effect on DSP -- so it lives alongside the other display
	// state rather than warranting its own subsystem.
	bool darkMode = false;

	BmboBTFQuantizer() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(ROOT_TRANSPOSE_PARAM, -1.f, 1.f, 0.f, "Root transpose", " st", 0.f, 12.f, 0.f);
		configParam(OCTAVE_SHIFT_PARAM, -4.f, 4.f, 0.f, "Octave shift", " oct");
		if (paramQuantities[OCTAVE_SHIFT_PARAM])
			paramQuantities[OCTAVE_SHIFT_PARAM]->snapEnabled = true;

		configInput(CV_INPUT, "1V/octave pitch");
		configInput(ROOT_CV_INPUT, "Root pitch (optional)");
		configOutput(CV_OUTPUT, "Quantized 1V/octave pitch");
		configBypass(CV_INPUT, CV_OUTPUT);
	}

	// Reads and parses a BTF file at `path`. On success, swaps it in as the
	// active tuning. On failure, the previously loaded tuning (if any) is
	// left completely untouched -- only the status message / LED change.
	// Never throws; malformed files simply fail to load.
	void loadFromPath(const std::string& path) {
		std::ifstream file(path, std::ios::in | std::ios::binary);
		if (!file.is_open()) {
			std::lock_guard<std::mutex> lock(quantizerMutex);
			statusMessage = "Could not open file";
			lastLoadSuccess = false;
			return;
		}

		std::ostringstream ss;
		ss << file.rdbuf();
		std::string text = ss.str();

		loadFromText(text, path);
	}

	void loadFromText(const std::string& text, const std::string& path) {
		ParseResult result = BTFParser::parse(text);

		std::lock_guard<std::mutex> lock(quantizerMutex);
		hasAttemptedLoad = true;
		if (result.success) {
			quantizer.setScale(result.scale);
			statusMessage = "Loaded \"" + result.scale.title + "\"";
			lastLoadSuccess = true;
			lastLoadedText = text;
			lastLoadedPath = path;
		}
		else {
			statusMessage = result.errorMessage;
			lastLoadSuccess = false;
			// Deliberately do not touch quantizer / lastLoadedText here:
			// the previous tuning (if any) remains active.
		}
	}

	void process(const ProcessArgs& args) override {
		int channels = std::max(inputs[CV_INPUT].getChannels(), 1);
		outputs[CV_OUTPUT].setChannels(channels);

		float rootTranspose = params[ROOT_TRANSPOSE_PARAM].getValue();
		float octaveShift = params[OCTAVE_SHIFT_PARAM].getValue();
		bool rootCvConnected = inputs[ROOT_CV_INPUT].isConnected();

		std::lock_guard<std::mutex> lock(quantizerMutex);

		double rootFreq = quantizer.hasScale() ? quantizer.getScale().rootFrequency : dsp::FREQ_C4;
		float rootVoltageFromHz = (float) std::log2(rootFreq / dsp::FREQ_C4);

		for (int c = 0; c < channels; c++) {
			float rootCv = rootCvConnected ? inputs[ROOT_CV_INPUT].getPolyVoltage(c) : 0.f;
			float effectiveRoot = rootVoltageFromHz + rootCv + rootTranspose;

			float in = inputs[CV_INPUT].getPolyVoltage(c);
			float relative = in - effectiveRoot;
			float quantized = quantizer.quantize(relative);
			float out = quantized + effectiveRoot + octaveShift;

			outputs[CV_OUTPUT].setVoltage(out, c);
		}

		// Green: a tuning is currently loaded and active.
		// Red: the most recent load attempt failed (previous tuning, if
		// any, is still the one active above -- see loadFromText()).
		lights[STATUS_LIGHT + 0].setBrightness(lastLoadSuccess ? 1.f : 0.f);
		lights[STATUS_LIGHT + 1].setBrightness((hasAttemptedLoad && !lastLoadSuccess) ? 1.f : 0.f);
	}

	// --- Persistence: keep the loaded tuning across save/reload ---------

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		if (!lastLoadedText.empty()) {
			json_object_set_new(rootJ, "btfText", json_stringn(lastLoadedText.c_str(), lastLoadedText.size()));
			json_object_set_new(rootJ, "btfPath", json_string(lastLoadedPath.c_str()));
		}
		json_object_set_new(rootJ, "darkMode", json_boolean(darkMode));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* textJ = json_object_get(rootJ, "btfText");
		json_t* pathJ = json_object_get(rootJ, "btfPath");
		if (textJ) {
			std::string text = json_string_value(textJ);
			std::string path = pathJ ? json_string_value(pathJ) : "";
			loadFromText(text, path);
		}
		json_t* darkJ = json_object_get(rootJ, "darkMode");
		if (darkJ)
			darkMode = json_boolean_value(darkJ);
	}
};

// ============================================================================
// Theme
// ============================================================================
//
// A small shared palette so every widget below stays visually consistent
// and the dark/light toggle only has to be defined in one place. Modeled
// on a high-contrast, sharp-cornered, monospace-leaning reference design:
// off-white/black base, hairline borders, no color accents -- weight and
// contrast alone do the work.

struct Theme {
	NVGcolor bg;         // panel background
	NVGcolor panelLine;   // divider lines / hairline borders
	NVGcolor ink;         // primary text / strokes
	NVGcolor inkDim;      // secondary text
	NVGcolor pressed;     // pressed/active button state
	NVGcolor boxBg;       // display/button fill
};

static Theme getTheme(bool dark) {
	Theme t;
	if (dark) {
		t.bg = nvgRGB(0x14, 0x13, 0x12);
		t.panelLine = nvgRGB(0x3a, 0x38, 0x35);
		t.ink = nvgRGB(0xf0, 0xee, 0xe8);
		t.inkDim = nvgRGB(0x9a, 0x96, 0x8f);
		t.pressed = nvgRGB(0x55, 0x53, 0x50);
		t.boxBg = nvgRGB(0x1e, 0x1d, 0x1b);
	}
	else {
		t.bg = nvgRGB(0xf7, 0xf5, 0xf0);
		t.panelLine = nvgRGB(0x1a, 0x1a, 0x1a);
		t.ink = nvgRGB(0x14, 0x14, 0x14);
		t.inkDim = nvgRGB(0x6a, 0x67, 0x62);
		t.pressed = nvgRGB(0x45, 0x43, 0x40);
		t.boxBg = nvgRGB(0xff, 0xfe, 0xfc);
	}
	return t;
}

// Sharp-cornered rectangle helper: nearly everything in the reference
// design has hard right-angle corners and a 1px hairline border rather
// than rounded/soft edges, so this is used throughout instead of
// nvgRoundedRect.
static void sharpBox(NVGcontext* vg, float x, float y, float w, float h, NVGcolor fill, NVGcolor stroke, float strokeWidth = 1.f) {
	nvgBeginPath(vg);
	nvgRect(vg, x, y, w, h);
	nvgFillColor(vg, fill);
	nvgFill(vg);
	if (strokeWidth > 0.f) {
		nvgStrokeColor(vg, stroke);
		nvgStrokeWidth(vg, strokeWidth);
		nvgStroke(vg);
	}
}


//
// A small read-only panel showing the currently loaded tuning's metadata and
// any status/error message. Pure drawing code -- reads Module state under
// the same mutex the Module uses when writing it.

struct BTFDisplay : widget::TransparentWidget {
	BmboBTFQuantizer* module = nullptr;

	void draw(const DrawArgs& args) override {
		Theme t = getTheme(module && module->darkMode);

		sharpBox(args.vg, 0.f, 0.f, box.size.x, box.size.y, t.boxBg, t.panelLine, 1.f);

		std::string title = "NO TUNING LOADED";
		std::string intervalsLine = "--";
		std::string rootLine = "--";
		std::string structureLine = "--";
		std::string status = "Load a .btf file to begin";
		bool ok = false;

		if (module) {
			std::lock_guard<std::mutex> lock(module->quantizerMutex);
			if (module->quantizer.hasScale()) {
				const Scale& scale = module->quantizer.getScale();
				title = scale.title;
				std::transform(title.begin(), title.end(), title.begin(), ::toupper);
				intervalsLine = std::to_string(scale.numIntervals());
				char buf[64];
				snprintf(buf, sizeof(buf), "%.2f HZ", scale.rootFrequency);
				rootLine = buf;
				structureLine = scale.structureName();
				std::transform(structureLine.begin(), structureLine.end(), structureLine.begin(), ::toupper);
			}
			status = module->statusMessage;
			ok = module->lastLoadSuccess;
		}

		nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		nvgTextLetterSpacing(args.vg, 0.4f);

		float x = 7.f;
		float y = 8.f;

		// Title row (bold-weight caps, larger)
		nvgFontSize(args.vg, 10.5f);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		nvgFillColor(args.vg, t.ink);
		nvgText(args.vg, x, y, title.c_str(), nullptr);
		y += 13.f;

		// Hairline divider under the title
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, x, y);
		nvgLineTo(args.vg, box.size.x - x, y);
		nvgStrokeColor(args.vg, t.panelLine);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
		y += 6.f;

		// Label : value rows, matching the reference's "LABEL ... value" pattern
		auto row = [&](const char* label, const std::string& value) {
			nvgFontSize(args.vg, 8.f);
			nvgFillColor(args.vg, t.inkDim);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgText(args.vg, x, y, label, nullptr);
			nvgFillColor(args.vg, t.ink);
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
			nvgText(args.vg, box.size.x - x, y, value.c_str(), nullptr);
			y += 11.f;
		};
		row("INTERVALS", intervalsLine);
		row("ROOT", rootLine);
		row("STRUCTURE", structureLine);

		y += 4.f;
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, x, y);
		nvgLineTo(args.vg, box.size.x - x, y);
		nvgStrokeColor(args.vg, t.panelLine);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
		y += 7.f;

		// Status message. Bold ink on success, dim on failure/idle -- no
		// color-coding, just weight/contrast, to match the no-accent palette.
		nvgFontSize(args.vg, 8.f);
		nvgFillColor(args.vg, ok ? t.ink : t.inkDim);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		nvgTextBox(args.vg, x, y, box.size.x - 2 * x, status.c_str(), nullptr);

		nvgTextLetterSpacing(args.vg, 0.f);
	}
};

// ============================================================================
// Load button
// ============================================================================
//
// A minimal clickable widget that opens a native file dialog and forwards
// the chosen path to the Module. This is the ONLY place osdialog is used,
// keeping all other GUI-adjacent logic (and all of the DSP) free of it.

struct BTFLoadButton : widget::OpaqueWidget {
	BmboBTFQuantizer* module = nullptr;
	bool pressed = false;

	void draw(const DrawArgs& args) override {
		Theme t = getTheme(module && module->darkMode);

		// Filled, high-contrast button (like the reference's black "Generate"
		// button) -- ink-colored fill, background-colored text, no border
		// needed since the fill itself provides the contrast. Pressing
		// swaps to a mid-grey for immediate visual feedback.
		NVGcolor fill = pressed ? t.pressed : t.ink;
		NVGcolor label = t.bg;

		sharpBox(args.vg, 0.f, 0.f, box.size.x, box.size.y, fill, t.panelLine, 0.f);

		nvgFontSize(args.vg, 9.5f);
		nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		nvgTextLetterSpacing(args.vg, 0.6f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, label);
		nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f + 0.5f, "LOAD BTF", nullptr);
		nvgTextLetterSpacing(args.vg, 0.f);
	}

	void onButton(const event::Button& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			pressed = true;
			e.consume(this);
			openFileDialog();
		}
		else if (e.action == GLFW_RELEASE) {
			pressed = false;
		}
		OpaqueWidget::onButton(e);
	}

	void openFileDialog() {
		if (!module)
			return;

		std::string dir = module->lastLoadedPath.empty()
			? asset::user("")
			: system::getDirectory(module->lastLoadedPath);

		osdialog_filters* filters = osdialog_filters_parse("Bmbo Tuning Format (.btf):btf;All files (*):*");
		char* pathC = osdialog_file(OSDIALOG_OPEN, dir.c_str(), nullptr, filters);
		osdialog_filters_free(filters);

		if (pathC) {
			std::string path = pathC;
			free(pathC);
			module->loadFromPath(path);
		}
	}
};

// ============================================================================
// Panel chrome
// ============================================================================
//
// Draws the ENTIRE panel background, divider lines, and static labels in
// code rather than relying on a baked-in panel.svg. Two reasons: (1)
// NanoSVG (Rack's panel renderer) has unreliable <text> support, and (2)
// drawing the background here too is what makes runtime dark/light mode
// possible without shipping two separate SVGs -- this widget just repaints
// itself in the current theme's colors every frame, reading the toggle
// straight from the Module.

struct PanelChrome : widget::TransparentWidget {
	BmboBTFQuantizer* module = nullptr;

	void label(const DrawArgs& args, const Theme& t, float xMm, float yMm, const char* text, float size = 6.f, int align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE) {
		Vec p = mm2px(Vec(xMm, yMm));
		nvgFontSize(args.vg, size);
		nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		nvgTextLetterSpacing(args.vg, 0.5f);
		nvgTextAlign(args.vg, align);
		nvgFillColor(args.vg, t.inkDim);
		nvgText(args.vg, p.x, p.y, text, nullptr);
		nvgTextLetterSpacing(args.vg, 0.f);
	}

	void hline(const DrawArgs& args, const Theme& t, float yMm) {
		Vec a = mm2px(Vec(3.f, yMm));
		Vec b = mm2px(Vec(57.96f, yMm));
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, a.x, a.y);
		nvgLineTo(args.vg, b.x, b.y);
		nvgStrokeColor(args.vg, t.panelLine);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
	}

	void draw(const DrawArgs& args) override {
		Theme t = getTheme(module && module->darkMode);

		// Full panel background
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(args.vg, t.bg);
		nvgFill(args.vg);

		// Panel border
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f);
		nvgStrokeColor(args.vg, t.panelLine);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// A slightly heavier top edge, echoing the reference design's top
		// header-bar rule -- same ink color as everything else, just 2px
		// instead of 1px, so it reads as a deliberate accent without
		// introducing a new color.
		Vec topRuleSize = mm2px(Vec(60.96f, 0.6f));
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, topRuleSize.x, topRuleSize.y);
		nvgFillColor(args.vg, t.ink);
		nvgFill(args.vg);

		// Zone divider hairlines
		hline(args, t, 20.f);
		hline(args, t, 58.f);
		hline(args, t, 80.f);
		hline(args, t, 102.f);

		// Title
		nvgFontSize(args.vg, 8.5f);
		nvgFontFaceId(args.vg, APP->window->uiFont->handle);
		nvgTextLetterSpacing(args.vg, 1.f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, t.ink);
		Vec titlePos = mm2px(Vec(30.48f, 6.f));
		nvgText(args.vg, titlePos.x, titlePos.y, "BMBO BTF QUANTIZER", nullptr);
		nvgTextLetterSpacing(args.vg, 0.f);

		label(args, t, 16.f, 62.f, "ROOT TRANSPOSE");
		label(args, t, 45.f, 62.f, "OCTAVE SHIFT");
		label(args, t, 16.f, 85.f, "1V/OCT");
		label(args, t, 45.f, 85.f, "ROOT CV");
		label(args, t, 30.48f, 122.5f, "OUT");
	}
};

// ============================================================================
// ModuleWidget
// ============================================================================

struct BmboBTFQuantizerWidget : ModuleWidget {
	BmboBTFQuantizerWidget(BmboBTFQuantizer* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/panel.svg")));

		PanelChrome* panelChrome = createWidget<PanelChrome>(Vec(0, 0));
		panelChrome->box.size = box.size;
		panelChrome->module = module;
		addChild(panelChrome);

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Load button + status LED
		BTFLoadButton* loadButton = createWidget<BTFLoadButton>(mm2px(Vec(6.f, 10.f)));
		loadButton->box.size = mm2px(Vec(34.f, 8.f));
		loadButton->module = module;
		addChild(loadButton);

		addChild(createLightCentered<MediumLight<GreenRedLight>>(
			mm2px(Vec(52.f, 14.f)), module, BmboBTFQuantizer::STATUS_LIGHT));

		// Display
		BTFDisplay* display = createWidget<BTFDisplay>(mm2px(Vec(4.f, 22.f)));
		display->box.size = mm2px(Vec(52.96f, 34.f));
		display->module = module;
		addChild(display);

		// Knobs
		addParam(createParamCentered<RoundBlackKnob>(
			mm2px(Vec(16.f, 68.f)), module, BmboBTFQuantizer::ROOT_TRANSPOSE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(
			mm2px(Vec(45.f, 68.f)), module, BmboBTFQuantizer::OCTAVE_SHIFT_PARAM));

		// Inputs
		addInput(createInputCentered<PJ301MPort>(
			mm2px(Vec(16.f, 92.f)), module, BmboBTFQuantizer::CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(
			mm2px(Vec(45.f, 92.f)), module, BmboBTFQuantizer::ROOT_CV_INPUT));

		// Output
		addOutput(createOutputCentered<PJ301MPort>(
			mm2px(Vec(30.48f, 114.f)), module, BmboBTFQuantizer::CV_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		BmboBTFQuantizer* m = dynamic_cast<BmboBTFQuantizer*>(this->module);
		if (!m)
			return;

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Dark mode", CHECKMARK(m->darkMode), [m]() {
			m->darkMode ^= true;
		}));
	}
};

Model* modelBmboBTFQuantizer = createModel<BmboBTFQuantizer, BmboBTFQuantizerWidget>("BTFQuantizer");
