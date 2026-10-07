// No plug-in window on this platform (Linux): the host shows its own parameter list.
#include "gui_view.hpp"
namespace sw::gui {
const char* platformApi() { return nullptr; }
std::unique_ptr<View> createView(const std::string&, std::function<std::string(const std::string&)>, double) { return nullptr; }
}  // namespace sw::gui
