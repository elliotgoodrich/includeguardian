#include "print_graph_factory.hpp"

#include <clang/AST/ASTConsumer.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graphviz.hpp>

#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>
#include <utility>

namespace IncludeGuardian {
namespace {

std::string to2Digit(int x)
{
	if (x < 10) {
		return "0" + std::to_string(x);
	}
	else {
		return std::to_string(x);
	}
}

std::string colorForSize(off_t bytes)
{
	const double log_e = std::clamp(std::log(static_cast<double>(bytes)) / 6.0, 1.0, 2.0);
	const double red = 99.0 * (log_e - 1.0);
	const double green = 99.0 - red;
	return "#" + to2Digit(red) + to2Digit(green) + "00";
	return "#990000";
}

int fontSizeForFileSize(off_t bytes)
{
	return 7.0 + 3.0 * std::log(static_cast<double>(bytes));
}

class IncludeScanner : public clang::PPCallbacks {
	struct Node {
		std::string path;
		off_t fileSizeInBytes = 0;
	};

	using Graph = boost::adjacency_list<
		boost::vecS,
		boost::vecS,
		boost::directedS,
		Node>;

	Graph m_graph;
	clang::SourceManager* m_sm;
	std::unordered_map<unsigned, Graph::vertex_descriptor> m_lookup;

	Graph::vertex_descriptor get_vertex_desc(const clang::FileEntry* file) {
		const auto it = m_lookup.find(file->getUID());
		if (it != m_lookup.end()) {
			return it->second;
		}
		else {
			return m_lookup.emplace(
				file->getUID(),
				add_vertex({file->getName().str(), file->getSize()}, m_graph)
			).first->second;
		}
	}

public:
	IncludeScanner(clang::SourceManager& sm)
		: m_graph()
		, m_sm(&sm)
		, m_lookup()
	{
	}

	void FileChanged(
		clang::SourceLocation Loc,
		FileChangeReason Reason,
		clang::SrcMgr::CharacteristicKind FileType,
		clang::FileID OptionalPrevFID) final
	{
	}

    void FileSkipped(const clang::FileEntryRef &SkippedFile,
                     const clang::Token &FilenameTok,
                     clang::SrcMgr::CharacteristicKind FileType) final
	{
	}

	void InclusionDirective(
		clang::SourceLocation HashLoc,
		const clang::Token& IncludeTok,
		clang::StringRef FileName,
		bool IsAngled,
		clang::CharSourceRange FilenameRange,
		const clang::FileEntry* File,
		clang::StringRef SearchPath,
		clang::StringRef RelativePath,
		const clang::Module* Imported,
		clang::SrcMgr::CharacteristicKind FileType) final
	{
		const clang::FileID fileID = m_sm->getFileID(HashLoc);
		add_edge(
			get_vertex_desc(m_sm->getFileEntryForID(fileID)),
			get_vertex_desc(File),
			m_graph);
	}

	void EndOfMainFile() final
	{
		write_graphviz(std::cout, m_graph, *this);
	}

	void operator()(std::ostream& out, Graph::vertex_descriptor v) const {
		const Node& n = m_graph[v];
		std::filesystem::path p(n.path);
		out << "[label=\"" << p.filename().string() << "\"]"
			"[style=\"filled\"]"
			"[fontcolor=\"#ffffff\"]"
			"[fillcolor=\"" << colorForSize(n.fileSizeInBytes) << "\"]"
			"[fontsize=\"" << fontSizeForFileSize(n.fileSizeInBytes) << "pt\"]";
	}
};

class Action : public clang::PreprocessOnlyAction {
	clang::ast_matchers::MatchFinder m_f;
	clang::CompilerInstance* m_ci = nullptr;

public:
	Action() = default;

	bool BeginInvocation(clang::CompilerInstance& ci) final
	{
		m_ci = &ci;
		return true;
	}

	std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
		clang::CompilerInstance& CI,
		clang::StringRef InFile) final
	{
		return m_f.newASTConsumer();
	}

	void ExecuteAction() final
	{
		getCompilerInstance().getPreprocessor().addPPCallbacks(
			std::make_unique<IncludeScanner>(m_ci->getSourceManager())
		);

		clang::PreprocessOnlyAction::ExecuteAction();
	}
};

} // close unnamed namespace

print_graph_factory::print_graph_factory() = default;

std::unique_ptr<clang::FrontendAction> print_graph_factory::create()
{
	return std::make_unique<Action>();
}

} // close IncludeGuardian namespace
