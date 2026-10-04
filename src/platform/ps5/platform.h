#pragma once

#include <rmlfocus/platform.h>

#include <cstdint>
#include <cstring>

extern "C" {
int sceUserServiceGetLoginUserIdList(int32_t* list);
int scePadOpen(int32_t user_id, int32_t port_type, int32_t index, const void* params);
int scePadClose(int32_t handle);
int scePadReadState(int32_t handle, void* data);
}

namespace playden::ps5 {

enum : std::uint32_t {
	PadOptions = 0x8,
	PadUp = 0x10,
	PadRight = 0x20,
	PadDown = 0x40,
	PadLeft = 0x80,
	PadL1 = 0x400,
	PadR1 = 0x800,
	PadTriangle = 0x1000,
	PadCircle = 0x2000,
	PadCross = 0x4000,
	PadIntercepted = 0x80000000u,
};

struct PadSample {
	std::uint32_t buttons;
	std::uint8_t left_x, left_y, right_x, right_y, l2, r2;
	std::uint8_t reserved[66];
	std::int32_t connected;
	std::uint64_t timestamp_us;
	std::uint8_t extension[16];
	std::uint8_t connected_count;
	std::uint8_t remaining[15];
};
static_assert(sizeof(PadSample) == 120, "unexpected scePad state layout");

class PadPlatform final : public rf::Platform {
public:
	PadPlatform(double (*clock)(), void (*log)(const char*, ...)) : clock_(clock), log_(log) {}
	~PadPlatform() override
	{
		for (auto& pad : pads_) if (pad.handle >= 0) scePadClose(pad.handle);
	}
	double Time() const override { return clock_(); }
	rf::InputState PollInput() override
	{
		const double now = Time();
		if (now - last_refresh_ > 1.0) RefreshUsers(now);
		rf::InputState result;
		int strongest = 0;
		for (auto& pad : pads_) {
			if (pad.handle < 0) continue;
			PadSample sample{};
			const bool connected = scePadReadState(pad.handle, &sample) == 0 && sample.connected;
			if (pad.connected && !connected) reset_ = true;
			pad.connected = connected;
			if (!connected || (sample.buttons & PadIntercepted)) { if (sample.buttons & PadIntercepted) reset_ = true; continue; }
			result.up |= sample.buttons & PadUp;
			result.down |= sample.buttons & PadDown;
			result.left |= sample.buttons & PadLeft;
			result.right |= sample.buttons & PadRight;
			result.select |= sample.buttons & PadCross;
			result.back |= sample.buttons & PadCircle;
			result.secondary |= sample.buttons & PadTriangle;
			result.previous_tab |= sample.buttons & PadL1;
			result.next_tab |= sample.buttons & PadR1;
			result.page_previous |= sample.l2 >= 128;
			result.page_next |= sample.r2 >= 128;
			const int dx = int(sample.left_x) - 128, dy = int(sample.left_y) - 128;
			if (dx * dx + dy * dy > strongest) {
				strongest = dx * dx + dy * dy;
				result.stick_x = dx / 128.f;
				result.stick_y = dy / 128.f;
			}
			options_held_ |= (sample.buttons & PadOptions) != 0;
		}
		return result;
	}
	bool TakeInputReset() override { const bool value = reset_; reset_ = false; return value; }
	bool OptionsHeld() { const bool value = options_held_; options_held_ = false; return value; }

private:
	struct Pad { std::int32_t user = -1, handle = -1; bool connected = false; };
	void RefreshUsers(double now)
	{
		last_refresh_ = now;
		std::int32_t users[4] = {-1, -1, -1, -1};
		if (sceUserServiceGetLoginUserIdList(users) != 0) return;
		for (auto& pad : pads_) {
			bool present = false;
			for (std::int32_t user : users) present |= user >= 0 && user == pad.user;
			if (pad.user >= 0 && !present) {
				if (pad.handle >= 0) scePadClose(pad.handle);
				pad = {};
				reset_ = true;
			}
		}
		for (std::int32_t user : users) {
			if (user < 0) continue;
			Pad* slot = nullptr;
			for (auto& pad : pads_) if (pad.user == user) slot = &pad;
			if (!slot) for (auto& pad : pads_) if (pad.user < 0) { slot = &pad; slot->user = user; break; }
			if (!slot || slot->handle >= 0) continue;
			slot->handle = scePadOpen(user, 0, 0, nullptr);
			log_("input: user %d pad handle 0x%x", user, slot->handle);
		}
	}

	double (*clock_)();
	void (*log_)(const char*, ...);
	Pad pads_[4];
	double last_refresh_ = -10.0;
	bool reset_ = false;
	bool options_held_ = false;
};

} // namespace playden::ps5
