#pragma once
/**
 * @file ota.h
 * @brief Over-the-air firmware update, pulled from a published manifest.
 *
 * Pull rather than push: nothing has to be able to reach the bridge, which
 * matters because it spends its life on a home network behind NAT and its
 * radio is off by default.
 *
 * **Trust model, stated plainly.** The downloaded image is verified against
 * the SHA-256 in the manifest before anything is committed, so a corrupted or
 * truncated download cannot be installed. That is an *integrity* check, not an
 * *authenticity* one: whoever controls the manifest URL controls what this
 * device installs. TLS certificates are not pinned. Signed images are the
 * right answer and are not implemented.
 *
 * Guarded accordingly: OTA runs only in developer mode, only with the belt
 * stopped, and writes to the inactive app slot so a failure leaves the running
 * firmware untouched.
 */

#include <stddef.h>
#include <stdint.h>

namespace ota {

/** @brief Where an update attempt has got to. */
enum class State : uint8_t {
    Idle,        ///< Nothing has been attempted since boot.
    Checking,    ///< Fetching the manifest.
    UpToDate,    ///< The manifest offers the version already running.
    Available,   ///< A different version is offered; apply() will install it.
    Installing,  ///< Downloading and writing to the inactive slot.
    Failed,      ///< Something went wrong; message() says what.
};

/** @brief Reads the running version and the stored manifest URL. */
void begin();

/**
 * @brief The firmware version this build was compiled with.
 * @return The version string; "dev-local" for an untagged build.
 */
const char* version();

/**
 * @brief Where the last update attempt got to.
 * @return The current @ref State.
 */
State state();

/**
 * @brief A human-readable account of the last outcome.
 * @return One line, for the console and the UI.
 */
const char* message();

/**
 * @brief The version the manifest offers.
 * @return The offered version, or "" when no manifest has been read.
 */
const char* availableVersion();

/**
 * @brief Where updates are looked for.
 * @return The stored manifest URL, or "" when none is set.
 */
const char* url();

/**
 * @brief Store the manifest URL in NVS.
 * @param u Absolute http:// or https:// URL, or "" to forget it.
 */
void setUrl(const char* u);

/**
 * @brief Fetch the manifest and compare versions.
 *
 * Blocks for the duration of one small HTTP request. Requires developer mode
 * and an associated network.
 */
void check();

/**
 * @brief Download the offered image and write it to the inactive slot.
 *
 * Blocks for the whole download, so the caller must ensure the belt is
 * stopped first: the belt link will go stale meanwhile. Verifies SHA-256
 * before committing, and reboots on success. Returns normally on failure,
 * leaving the running firmware untouched.
 */
void apply();

}  // namespace ota
