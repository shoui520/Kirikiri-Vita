#include "krkrvita/vita_input_bridge.hpp"

#include "CharacterSet.h"
#include "DebugIntf.h"
#include "StorageIntf.h"
#include "SysInitImpl.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace {

struct InputConfiguration {
	std::map<std::string, std::string> bindings;
	float deadzone = 0.18f;
	float cursor_speed = 780.0f;
	bool touch_enabled = true;
};

InputConfiguration configuration;
Uint32 previous_tick = 0;

std::string trim(std::string value)
{
	const std::string::size_type first = value.find_first_not_of(" \t\r\n");
	if(first == std::string::npos) return std::string();
	const std::string::size_type last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

void set_defaults()
{
	configuration = InputConfiguration();
	configuration.bindings["cross"] = "mouse_left";
	configuration.bindings["circle"] = "mouse_right";
	configuration.bindings["square"] = "key_space";
	configuration.bindings["triangle"] = "key_escape";
	configuration.bindings["ltrigger"] = "key_pageup";
	configuration.bindings["rtrigger"] = "key_pagedown";
	configuration.bindings["dpad_up"] = "key_up";
	configuration.bindings["dpad_down"] = "key_down";
	configuration.bindings["dpad_left"] = "key_left";
	configuration.bindings["dpad_right"] = "key_right";
	configuration.bindings["start"] = "key_enter";
	configuration.bindings["select"] = "menu";
	configuration.bindings["left_stick"] = "mouse_cursor";
	configuration.bindings["front_touch"] = "mouse_absolute";
}

const char *button_name(Uint8 button)
{
	switch(button)
	{
	case SDL_CONTROLLER_BUTTON_A: return "cross";
	case SDL_CONTROLLER_BUTTON_B: return "circle";
	case SDL_CONTROLLER_BUTTON_X: return "square";
	case SDL_CONTROLLER_BUTTON_Y: return "triangle";
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return "ltrigger";
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return "rtrigger";
	case SDL_CONTROLLER_BUTTON_DPAD_UP: return "dpad_up";
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "dpad_down";
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "dpad_left";
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "dpad_right";
	case SDL_CONTROLLER_BUTTON_START: return "start";
	case SDL_CONTROLLER_BUTTON_BACK: return "select";
	case SDL_CONTROLLER_BUTTON_LEFTSTICK: return "left_stick_button";
	case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return "right_stick_button";
	default: return nullptr;
	}
}

SDL_Keycode action_key(const std::string &action)
{
	if(action == "key_space") return SDLK_SPACE;
	if(action == "key_enter") return SDLK_RETURN;
	if(action == "key_escape" || action == "menu") return SDLK_ESCAPE;
	if(action == "key_pageup") return SDLK_PAGEUP;
	if(action == "key_pagedown") return SDLK_PAGEDOWN;
	if(action == "key_up") return SDLK_UP;
	if(action == "key_down") return SDLK_DOWN;
	if(action == "key_left") return SDLK_LEFT;
	if(action == "key_right") return SDLK_RIGHT;
	if(action == "key_control") return SDLK_LCTRL;
	if(action == "key_shift") return SDLK_LSHIFT;
	if(action == "key_tab") return SDLK_TAB;
	if(action == "key_backspace") return SDLK_BACKSPACE;
	return SDLK_UNKNOWN;
}

std::string binding(const char *source)
{
	const std::map<std::string, std::string>::const_iterator found =
		configuration.bindings.find(source ? source : "");
	return found == configuration.bindings.end() ? std::string() : found->second;
}

bool parse_bool(const std::string &value)
{
	return value == "1" || value == "true" || value == "yes" || value == "on";
}

void load_profile(const std::string &path)
{
	std::ifstream stream(path.c_str());
	if(!stream)
	{
		TVPAddImportantLog(ttstr(TJS_W("Cannot open Vita input profile; using defaults")));
		return;
	}
	std::string line;
	while(std::getline(stream, line))
	{
		line = trim(line);
		if(line.empty() || line[0] == '#' || line[0] == ';') continue;
		const std::string::size_type separator = line.find('=');
		if(separator == std::string::npos) continue;
		const std::string key = trim(line.substr(0, separator));
		const std::string value = trim(line.substr(separator + 1));
		try
		{
			if(key.compare(0, 5, "bind.") == 0)
				configuration.bindings[key.substr(5)] = value;
			else if(key == "analog_deadzone")
				configuration.deadzone = std::max(0.0f, std::min(0.95f, std::stof(value)));
			else if(key == "cursor_speed")
				configuration.cursor_speed = std::max(1.0f, std::min(4000.0f, std::stof(value)));
			else if(key == "touch_enabled")
				configuration.touch_enabled = parse_bool(value);
		}
		catch(...)
		{
			TVPAddImportantLog(ttstr(TJS_W("Ignoring invalid Vita input profile value")));
		}
	}
	TVPAddImportantLog(ttstr(TJS_W("Loaded per-game Vita controller profile")));
}

} // namespace

void krkrvita_input_initialize()
{
	set_defaults();
	previous_tick = SDL_GetTicks();

	tTJSVariant option;
	ttstr profile_path;
	if(TVPGetCommandLine(TJS_W("-krkrprofile"), &option)) profile_path = option;
	if(profile_path.IsEmpty()) return;

	std::string utf8_path;
	if(TVPUtf16ToUtf8(utf8_path, profile_path.AsStdString())) load_profile(utf8_path);
}

bool krkrvita_input_translate(SDL_Event &event)
{
	if(!configuration.touch_enabled)
	{
		if(event.type == SDL_FINGERDOWN || event.type == SDL_FINGERUP ||
			event.type == SDL_FINGERMOTION) return false;
		if((event.type == SDL_MOUSEMOTION && event.motion.which == SDL_TOUCH_MOUSEID) ||
			((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) &&
			 event.button.which == SDL_TOUCH_MOUSEID)) return false;
	}

	if(event.type != SDL_CONTROLLERBUTTONDOWN && event.type != SDL_CONTROLLERBUTTONUP)
		return true;
	const std::string action = binding(button_name(event.cbutton.button));
	if(action.empty() || action == "pad") return true;
	if(action == "disabled" || action == "mouse_cursor") return false;

	const Uint32 original_type = event.type;
	const bool pressed = original_type == SDL_CONTROLLERBUTTONDOWN;
	const SDL_Keycode key = action_key(action);
	if(key != SDLK_UNKNOWN)
	{
		SDL_zero(event.key);
		event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
		event.key.type = event.type;
		event.key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
		event.key.repeat = 0;
		event.key.keysym.sym = key;
		event.key.keysym.scancode = SDL_GetScancodeFromKey(key);
		event.key.keysym.mod = KMOD_NONE;
		return true;
	}

	Uint8 mouse_button = 0;
	if(action == "mouse_left") mouse_button = SDL_BUTTON_LEFT;
	else if(action == "mouse_right") mouse_button = SDL_BUTTON_RIGHT;
	else if(action == "mouse_middle") mouse_button = SDL_BUTTON_MIDDLE;
	if(mouse_button)
	{
		int x = 0;
		int y = 0;
		SDL_GetMouseState(&x, &y);
		SDL_zero(event.button);
		event.type = pressed ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
		event.button.type = event.type;
		event.button.which = 0;
		event.button.button = mouse_button;
		event.button.state = pressed ? SDL_PRESSED : SDL_RELEASED;
		event.button.clicks = 1;
		event.button.x = x;
		event.button.y = y;
		return true;
	}
	return false;
}

void krkrvita_input_pump(SDL_Window *window, SDL_GameController *controller)
{
	const Uint32 now = SDL_GetTicks();
	const float seconds = std::min(0.05f, (now - previous_tick) / 1000.0f);
	previous_tick = now;
	if(!window || !controller || binding("left_stick") != "mouse_cursor") return;

	float x_axis = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
	float y_axis = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
	const float magnitude = std::sqrt(x_axis * x_axis + y_axis * y_axis);
	if(magnitude <= configuration.deadzone) return;
	const float normalized = std::min(1.0f,
		(magnitude - configuration.deadzone) / (1.0f - configuration.deadzone));
	x_axis = x_axis / magnitude * normalized;
	y_axis = y_axis / magnitude * normalized;

	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	SDL_GetMouseState(&x, &y);
	SDL_GetWindowSize(window, &width, &height);
	x += static_cast<int>(x_axis * configuration.cursor_speed * seconds);
	y += static_cast<int>(y_axis * configuration.cursor_speed * seconds);
	x = std::max(0, std::min(width - 1, x));
	y = std::max(0, std::min(height - 1, y));
	SDL_WarpMouseInWindow(window, x, y);
}
