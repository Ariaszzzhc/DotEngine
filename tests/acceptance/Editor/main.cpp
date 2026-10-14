#include <exception>
#include <filesystem>
#include <print>
#include <string>
#include <string_view>

#if defined(_WIN32)
#  include <SDL3/SDL_main.h>
#endif

namespace Dot::Editor {
  auto testModelImport(const std::filesystem::path& parent) -> int;
  auto testViewport(const std::filesystem::path& parent) -> int;
  auto testWorkspace(const std::filesystem::path& parent) -> int;
  auto testGui(const std::filesystem::path& parent) -> int;
  auto testDocument(const std::filesystem::path& parent, bool render) -> int;
  auto testProject(const std::filesystem::path& compiler, const std::filesystem::path& parent,
                   bool render) -> int;
}  // namespace Dot::Editor

namespace {
  auto path(std::string_view text) -> std::filesystem::path {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
  }
}  // namespace

/** Development-only native editor acceptance dispatcher.
 * 仅用于开发的原生编辑器验收分发入口。 */
auto main(int argc, char** argv) -> int {
  try {
    const std::string_view command = argc > 1 ? argv[1] : "";

    if (argc == 3 && command == "--model-import-test") {
      return Dot::Editor::testModelImport(path(argv[2]));
    }

    if (argc == 3 && command == "--viewport-test") {
      return Dot::Editor::testViewport(path(argv[2]));
    }

    if (argc == 3 && command == "--workspace-test") {
      return Dot::Editor::testWorkspace(path(argv[2]));
    }

    if (argc == 3 && command == "--gui-test") {
      return Dot::Editor::testGui(path(argv[2]));
    }

    if (argc == 3 && (command == "--document-test" || command == "--document-render-test")) {
      return Dot::Editor::testDocument(path(argv[2]), command == "--document-render-test");
    }

    if (argc == 4 && (command == "--project-test" || command == "--project-render-test")) {
      return Dot::Editor::testProject(path(argv[2]), path(argv[3]),
                                      command == "--project-render-test");
    }

    std::println(stderr, "Usage: DotEditorAcceptance <acceptance-command> [arguments]");
    return 2;
  } catch (const std::exception& error) {
    std::println(stderr, "Editor acceptance failed: {}", error.what());
    return 1;
  }
}
