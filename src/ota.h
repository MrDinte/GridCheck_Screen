// Over-the-air firmware updates.
//
// The flash holds two copies of the firmware (ota_0 / ota_1). An update is
// written into the copy that isn't running, then the board restarts into it.
// If the new version doesn't reach "running normally" (screen up for a minute),
// the bootloader goes back to the previous one on the next restart.
//
// Two ways in:
//   - online: the board reads a small JSON file (version + firmware address)
//     from the update address and installs it when asked;
//   - upload: the phone settings page sends a firmware file.
#pragma once

#include <cstdint>
#include <string>

namespace ota {

enum class State { Idle, Checking, Downloading, Writing, Done, Failed };

struct Status {
  State state = State::Idle;
  int percent = 0;
  std::string latest;   // newest version seen online ("" = not checked / none)
  std::string message;  // last result, for the screen and the phone page
};

/** Start the background checker and confirm this firmware once it has run for a while. */
void begin();
Status status();
/** Is `latest` newer than this firmware? */
bool updateAvailable();
/** Check the update address now (non-blocking). */
void checkNow();
/** Download and install the latest online version (non-blocking). */
bool installOnline(std::string& err);

// Upload from the phone page, fed chunk by chunk by the web server.
bool uploadBegin(size_t size, std::string& err);
bool uploadWrite(const void* data, size_t len, std::string& err);
bool uploadEnd(std::string& err);  // on success the caller restarts the board
void uploadAbort();

/** Default place to look for updates (the GridCheck firmware repo's GitHub Pages site). */
extern const char* const DEFAULT_URL;

}  // namespace ota
