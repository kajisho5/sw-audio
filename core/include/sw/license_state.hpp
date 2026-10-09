// SW AUDIO core — is this product licensed on this computer? The licence files (sw/license.hpp) in the user's licence folder are checked
// with the public keys built into the plug-ins. A product is covered by a licence for its own code (e.g. "in07"), for its line ("studio",
// "live": the LV products are LIVE, the rest STUDIO, as in docs/project/03_product_lineup.csv; IN07 counts as STUDIO like IN01-IN06) or
// for "all". Without a licence the plug-in plays with the demo silence (sw/demo_gate.hpp).
// A development build is never in demo: enforcement needs the build option SW_LICENSE_ENFORCE and at least one built-in public key
// (none yet: the production key is made by the owner, core/src/license_state.cpp says where it goes). The environment variable
// SW_LICENSE_TEST_DEMO=1 puts any build in demo (tests); nothing outside turns the demo off.
// Folder: Windows %APPDATA%\SEVENTHWELL\Licenses, macOS ~/Library/Application Support/SEVENTHWELL/Licenses,
//         Linux $XDG_DATA_HOME (or ~/.local/share)/SEVENTHWELL/Licenses. Files *.swlicense, at most 64, 8 KiB each.
#pragma once
#include "sw/license.hpp"
#include <string>
#include <vector>

namespace sw::license {

struct Verdict {
    bool enforced = false;   // this build checks licences at all
    bool licensed = true;    // play without the demo silence
    Status status = Status::NotALicense;   // of the best file found
    std::string detail;      // for the screen: the licence id and products, or why not
};

std::vector<std::string> acceptedProducts(const std::string& code);   // {code, line, "all"}, lower case
// the files in a folder against keys (tests; productState uses it)
Verdict evaluate(const std::string& folder, const PublicKey* keys, size_t numKeys, const std::vector<std::string>& accepted, int pluginMajor,
                 const std::string& machine);
// this product on this computer (main thread: reads the licence folder; call at activation)
Verdict productState(const std::string& code, int pluginMajor);
bool enforced();
bool forcedDemo();
std::string licenseFolder();
std::string thisMachine();   // machineHash(kMachineSalt), worked out once

// a licence file the user gives (the plug-in window: a file, or what the activation server sent back), checked as the folder's files are
// (the keys, a product of this plug-in, the major version, this computer). Valid: written into the folder (made when missing) as
// "<licence id, cleaned into a file name>.swlicense", replacing a file of the same id; anything else writes nothing. out as check() fills it.
Status install(const std::string& folder, std::string_view text, const PublicKey* keys, size_t numKeys, const std::vector<std::string>& accepted,
               int pluginMajor, const std::string& machine, License& out, std::string& error);
// the same for this product on this computer: the built-in keys, licenseFolder()
Status installForProduct(std::string_view text, const std::string& code, int pluginMajor, License& out, std::string& error);

}  // namespace sw::license
