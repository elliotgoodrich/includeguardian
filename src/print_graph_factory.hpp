#ifndef INCLUDE_GUARD_9E4C7C9E_6B70_4EF5_B47D_6B12DDDA7316
#define INCLUDE_GUARD_9E4C7C9E_6B70_4EF5_B47D_6B12DDDA7316

#include <clang/Tooling/Tooling.h>

namespace IncludeGuardian {

/// This component is a concrete implementation of a `FrontEndActionFactory`
/// that will print out a DOT file representing a DAG of the include
/// directives.
class print_graph_factory : public clang::tooling::FrontendActionFactory {
public:
	/// Create a `print_graph_factory`.
	print_graph_factory();

	/// Returns a new `clang::FrontendAction`.
	std::unique_ptr<clang::FrontendAction> create() final;
};

} // close IncludeGuardian namespace

#endif
