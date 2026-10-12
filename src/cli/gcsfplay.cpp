// SPDX-License-Identifier: MIT

// gcsfplay: renders .minigcsf and .gcsf files to WAV or FLAC at 32,028 Hz, the audio interface's rate.
//
// usage: gcsfplay [options] <file> [<file> ...]
//   -o, --output FILE  the output file, WAV or FLAC by its extension (default: the input's name with .wav, in the same
//                      folder). Only valid with a single input.
//   --length T         override the length tag (seconds, m:s or h:m:s)
//   --fade T           override the fade tag
//   --default T        the length when there's no length tag (default and fade: PlaybackOptions)
//   --info             print the tags and loaded files without rendering
//   --version          print the version
//
// The files are rendered one after another. A file that fails doesn't stop the rest. A .gcsflib among the inputs is
// skipped. It's a library that the other files load. Files dropped on the program become inputs.
//
// The tool runs on xsflib's play-tool frame (xsflib/cli/play.h). This file holds GCSF's names, help and --info
// summary.

#include "xsflib/cli/play.h"

#include "gcsf/player.h"

using namespace gcsf;

namespace
{

// The help's description of what the tool does.
constexpr char kDescription[] =
    "Renders each .minigcsf or .gcsf file to a WAV file next to it at 32,028 Hz. The length and\n"
    "fade come from the file's tags. Libraries (.gcsflib) are skipped because the .minigcsf files\n"
    "load them.\n"
    "You can also drop files on the program.\n";

} // namespace

int main(int argc, char** argv)
{
    xsflib::cli::PlayTool<Player> tool;
    tool.name = "gcsfplay";
    tool.version = GCSF_VERSION;
    tool.description = kDescription;
    tool.output_option = "write FILE instead. Its extension selects WAV or FLAC (one input only)";
    tool.info_option = "print the tags and the files that each input loads, without rendering";
    tool.library_extension = ".gcsflib";
    tool.mini_extension = ".minigcsf";
    tool.a_file = "a GCSF file";
    tool.describe_set = xsflib::cli::DescribeLoadedChunks<Player>;

    return xsflib::cli::RunPlayTool(tool, argc, argv);
}
