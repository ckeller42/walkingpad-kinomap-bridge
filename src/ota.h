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
 * @return The current ota::State.
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
 * @return The stored manifest URL, or OTA_MANIFEST_URL when none is stored.
 */
const char* url();

/**
 * @brief Whether url() is the compiled-in default rather than a stored one.
 * @return True when url() equals OTA_MANIFEST_URL.
 */
bool usingDefaultUrl();

/**
 * @brief Store the manifest URL in NVS.
 * @param u Absolute http:// or https:// URL, or "" to go back to the default
 *          OTA_MANIFEST_URL.
 */
void setUrl(const char* u);

/**
 * @brief The PlatformIO environment this firmware was built for.
 * @return The compiled-in FW_ENV, e.g. "cores3" or "cores3_sim".
 */
const char* env();

/**
 * @brief The environment the last-read manifest's image was built for.
 * @return The manifest's `env` field, or "" when absent or not yet read.
 */
const char* imageEnv();

/**
 * @brief Whether the offered image was built for this firmware's environment.
 *
 * A manifest without `env` never matches, so a bench build cannot be
 * overwritten by an image of unknown origin.
 * @return True when imageEnv() is non-empty and equals env().
 */
bool envMatches();

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
 * leaving the running firmware untouched. Refuses an image built for a
 * different environment (see envMatches()).
 *
 * @param progress Called from the download loop, on the calling task, each
 *                 time the integer percentage changes; may be nullptr.
 */
void apply(void (*progress)(int percent));

/** @brief apply() without a progress callback, for the serial console. */
void apply();

}  // namespace ota
