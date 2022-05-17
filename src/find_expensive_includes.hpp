#ifndef INCLUDE_GUARD_0DC4C9E1_CE28_4D0C_9771_86480E7D991D
#define INCLUDE_GUARD_0DC4C9E1_CE28_4D0C_9771_86480E7D991D

#include <clang/Tooling/Tooling.h>

#include <filesystem>
#include <vector>

namespace IncludeGuardian {

struct include_directive {
	std::filesystem::path file;
	std::string include;
	std::size_t savingInBytes;
};

/// This component is a concrete implementation of a `FrontEndActionFactory`
/// that will output the include directives along with the total file size
/// that would be saved if it was deleted.
class find_expensive_includes : public clang::tooling::FrontendActionFactory {
	std::vector<include_directive>* m_out;
public:
	/// Create a `print_graph_factory`.
	explicit find_expensive_includes(std::vector<include_directive>& out);

	/// Returns a new `clang::FrontendAction`.
	std::unique_ptr<clang::FrontendAction> create() final;
};

} // close IncludeGuardian namespace

#endif