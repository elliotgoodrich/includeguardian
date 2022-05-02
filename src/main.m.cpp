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

#include <iostream>
#include <string>

class IncludeScanner : public clang::PPCallbacks {
	int m_indent = -1;
public:
	IncludeScanner() = default;

	void FileChanged(
		clang::SourceLocation Loc,
		FileChangeReason Reason,
		clang::SrcMgr::CharacteristicKind FileType,
		clang::FileID PrevFID) final
	{
	    switch (Reason) {
	    case clang::PPCallbacks::EnterFile:
		    ++m_indent;
		    break;
	    case clang::PPCallbacks::ExitFile:
		    --m_indent;
		    break;
	    case clang::PPCallbacks::SystemHeaderPragma:
		    break;
	    case clang::PPCallbacks::RenameFile:
		    break;
	    }
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
		std::cout << std::string(m_indent * 2, ' ')
			      << "Found #include <"
			      << std::string_view(FileName)
			      << ">\n";
	}

	void EndOfMainFile() final {
		std::cout << "Done!\n";
	}
};

class Action : public clang::PreprocessOnlyAction {
	clang::ast_matchers::MatchFinder m_f;

public:
	Action() = default;

	bool BeginInvocation(clang::CompilerInstance& ci) final
	{
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
			std::make_unique<IncludeScanner>()
		);

		clang::PreprocessOnlyAction::ExecuteAction();
	}
};

int main(int argc, const char** argv) {
	llvm::cl::OptionCategory MyToolCategory("my-tool options");

	llvm::Expected<clang::tooling::CommonOptionsParser> optionsParser =
		clang::tooling::CommonOptionsParser::create(argc, argv, MyToolCategory);
	if (!optionsParser) {
		std::cerr << toString(optionsParser.takeError());
		return 1;
	}
	clang::tooling::ClangTool Tool(optionsParser->getCompilations(),
		optionsParser->getSourcePathList());
	return Tool.run(clang::tooling::newFrontendActionFactory<Action>().get());
}