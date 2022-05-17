#include "find_expensive_includes.hpp"

#include "reachability_graph.hpp"

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

#include <cassert>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>
#include <utility>

namespace IncludeGuardian {
namespace {

class IncludeScanner : public clang::PPCallbacks {
	struct Node {
		std::string path;
		off_t fileSizeInBytes = 0;
	};

	// Include name
	using Edge = std::string;

	using Graph = boost::adjacency_list<
		boost::vecS,
		boost::vecS,
		boost::directedS,
		Node,
	    Edge>;

	Graph m_graph;
	clang::SourceManager* m_sm;
	std::unordered_map<unsigned, Graph::vertex_descriptor> m_lookup;
	std::vector<include_directive>* m_out;
	std::vector<Graph::vertex_descriptor> m_sources;

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
	IncludeScanner(clang::SourceManager& sm, std::vector<include_directive>* out)
		: m_graph()
		, m_sm(&sm)
		, m_lookup()
		, m_out(out)
	{
	}

	void FileChanged(
		clang::SourceLocation Loc,
		FileChangeReason Reason,
		clang::SrcMgr::CharacteristicKind FileType,
		clang::FileID OptionalPrevFID) final
	{
		if (Reason == FileChangeReason::EnterFile) {
			if (m_sm->isInMainFile(Loc)) {
				// TODO: Avoid the extra lookup
				const clang::FileID fileID = m_sm->getFileID(Loc);
				if (const clang::FileEntry* file = m_sm->getFileEntryForID(fileID)) {
					const auto it = m_lookup.find(file->getUID());
					if (it == m_lookup.end()) {
						m_sources.push_back(get_vertex_desc(file));
					}
				}
			}
		}
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
			FileName.str(),
			m_graph);
	}

	void EndOfMainFile() final
	{
		reachability_graph dag(m_graph);
		for (const Graph::vertex_descriptor v : boost::make_iterator_range(vertices(m_graph))) {
			for (const Graph::edge_descriptor directive : boost::make_iterator_range(out_edges(v, m_graph))) {
				const Graph::vertex_descriptor include = target(directive, m_graph);
				std::size_t bytes_saved = 0;

				for (const Graph::vertex_descriptor source : m_sources) {
					// Find the sum of the file sizes for `include` and all its
					// includes that are now not reachable if we removed it.
					for (const Graph::vertex_descriptor i : dag.reachable_from(v)) {
						// `i` is reachable from `source` only through `include` if the
						// number of paths between `source` and `i` is exactly the product
						// of the number of paths between `source` and `include`, and the
						// number of paths between `include` and `i`.
						if (dag.number_of_paths(source, include) *
							dag.number_of_paths(include, i) ==
							dag.number_of_paths(source, i)) {
							bytes_saved += m_graph[include].fileSizeInBytes;
						}
					}
				}

				m_out->emplace_back(
					std::filesystem::path(m_graph[v].path),
					m_graph[directive],
					bytes_saved);
			}
		}
	}
};

class ExpensiveAction : public clang::PreprocessOnlyAction {
	clang::ast_matchers::MatchFinder m_f;
	clang::CompilerInstance* m_ci = nullptr;
	std::vector<include_directive>* m_out;

public:
	ExpensiveAction(std::vector<include_directive>* out)
	: m_out(out)
	{
	}

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
			std::make_unique<IncludeScanner>(m_ci->getSourceManager(), m_out)
		);

		clang::PreprocessOnlyAction::ExecuteAction();
	}
};

} // close unnamed namespace

find_expensive_includes::find_expensive_includes(std::vector<include_directive>& out)
: m_out(&out)
{
	assert(out.empty());
}

std::unique_ptr<clang::FrontendAction> find_expensive_includes::create()
{
	return std::make_unique<ExpensiveAction>(m_out);
}

} // close IncludeGuardian namespace
