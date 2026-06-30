# Editor integrations. The VS Code grammar is the CANONICAL Eskiu TextMate grammar;
# the IntelliJ bundle reuses it. Run `make sync-grammar` after any language-syntax change.

GRAMMAR := vscode/syntaxes/eskiu.tmLanguage.json

.PHONY: sync-grammar
sync-grammar:
	cp $(GRAMMAR) intellij/bundle/Syntaxes/eskiu.tmLanguage.json
	cp $(GRAMMAR) intellij/plugin/src/main/resources/bundles/eskiu/Syntaxes/eskiu.tmLanguage.json
	@echo "IntelliJ bundle grammar synced from $(GRAMMAR)"
