// The /M-TAGS command line.
//
// The original's handler recognised the switch by comparing the first seven
// characters of each argument against "/M-TAGS" and - unlike every other token -
// only accepting it when the argument is exactly that long, so "/M-TAGSFOO" is
// not ours. Every argument that followed was a folder, and each one became a
// same-folder document there and then (clean.cpp FUN_10020920), so "/M-TAGS a b"
// is two folders and nothing else. "Separate folder" is a File menu command only,
// in the original and here.

#include "creator.h"
#include "log.h"
#include "text.h"

#include <SDK/foobar2000.h>

#include <string>

namespace {

using namespace mtags;

class t_commandline : public commandline_handler {
public:
    commandline_handler::result on_token(const char* token) override {
        const char* const command = "/M-TAGS";

        // The original uppercased the whole argument before comparing, so the switch
        // is case-insensitive, and it required the exact switch, so "/M-TAGSFOO" is
        // not ours. The helper is the same pfc-backed one the rest of the plugin uses
        // for names.
        const std::string upper = uppercase(token ? token : "");
        if (upper != command) {
            return RESULT_NOT_OURS;
        }

        log_verbose("Command line: received m-TAGS command");
        m_active = true;
        return RESULT_PROCESSED_EXPECT_FILES;
    }

    // Each folder is acted on as it arrives, which is what the original did: one
    // path, one document, in that folder. Nothing is collected, so a second path is
    // simply another folder to document rather than a destination.
    void on_file(const char* url) override {
        if (!m_active) {
            return;
        }
        log_verbose("Command line: processing %s", url);
        create_folder_wide_for(url ? url : "");
    }

    // The switch's group of paths has ended; a later /M-TAGS starts a fresh one.
    void on_files_done() override {
        m_active = false;
    }

    bool want_directories() override { return true; }

private:
    bool m_active = false;
};

FB2K_SERVICE_FACTORY(t_commandline);

} // namespace
