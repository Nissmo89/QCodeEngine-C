#include <QCoreApplication>
#include <QFile>
#include <QDebug>
#include "CodeEditor/FoldQuery.h"
#include <tree_sitter/api.h>

extern "C" const TSLanguage* tree_sitter_c(void);

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    if (argc < 2) {
        qCritical() << "Usage:" << argv[0] << "<c_file>";
        return 1;
    }
    
    // Read the test file
    QFile file(argv[1]);
    if (!file.open(QIODevice::ReadOnly)) {
        qCritical() << "Failed to open file:" << argv[1];
        return 1;
    }
    QString source = QString::fromUtf8(file.readAll());
    file.close();
    
    // Initialize Qt resources
    Q_INIT_RESOURCE(queries);
    
    // Load fold query
    QFile queryFile(":/queries/c_folds.scm");
    if (!queryFile.open(QIODevice::ReadOnly)) {
        qCritical() << "Failed to open fold query";
        return 1;
    }
    QByteArray queryScheme = queryFile.readAll();
    
    // Create parser and parse
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_c());
    
    QByteArray utf8 = source.toUtf8();
    TSTree* tree = ts_parser_parse_string(parser, nullptr, utf8.constData(), utf8.size());
    
    // Create fold query and compute ranges
    FoldQuery foldQuery(tree_sitter_c(), queryScheme);
    if (!foldQuery.isValid()) {
        qCritical() << "Fold query is invalid";
        return 1;
    }
    
    std::vector<FoldRange> ranges = foldQuery.computeRanges(tree, utf8);
    
    // Print results
    qInfo() << "=== FOLD ANALYSIS FOR" << argv[1] << "===";
    qInfo() << "Total foldable regions:" << ranges.size();
    qInfo() << "";
    
    // Split source into lines for display
    QStringList lines = source.split('\n');
    
    // Sort ranges by start line
    std::sort(ranges.begin(), ranges.end(), [](const FoldRange& a, const FoldRange& b) {
        return a.startRow < b.startRow;
    });
    
    // Display each fold range with context
    for (size_t i = 0; i < ranges.size(); i++) {
        const FoldRange& fr = ranges[i];
        qInfo() << "Fold" << (i + 1) << "of" << ranges.size();
        qInfo() << "  Lines:" << (fr.startRow + 1) << "-" << (fr.endRow + 1);
        qInfo() << "  Span:" << (fr.endRow - fr.startRow) << "lines";
        
        // Show first line (opening)
        if (fr.startRow < (uint32_t)lines.size()) {
            QString firstLine = lines[fr.startRow].trimmed();
            if (firstLine.length() > 80) {
                firstLine = firstLine.left(77) + "...";
            }
            qInfo() << "  Start:" << firstLine;
        }
        
        // Show last line (closing)
        if (fr.endRow < (uint32_t)lines.size()) {
            QString lastLine = lines[fr.endRow].trimmed();
            if (lastLine.length() > 80) {
                lastLine = lastLine.left(77) + "...";
            }
            qInfo() << "  End:  " << lastLine;
        }
        qInfo() << "";
    }
    
    // Statistics
    int singleLine = 0;
    int shortFolds = 0;  // 2-5 lines
    int mediumFolds = 0; // 6-20 lines
    int longFolds = 0;   // 21+ lines
    
    for (const FoldRange& fr : ranges) {
        int span = fr.endRow - fr.startRow;
        if (span == 0) singleLine++;
        else if (span <= 5) shortFolds++;
        else if (span <= 20) mediumFolds++;
        else longFolds++;
    }
    
    qInfo() << "=== STATISTICS ===";
    qInfo() << "Single-line folds:" << singleLine << "(should be 0 - these are bugs!)";
    qInfo() << "Short folds (2-5 lines):" << shortFolds;
    qInfo() << "Medium folds (6-20 lines):" << mediumFolds;
    qInfo() << "Long folds (21+ lines):" << longFolds;
    
    // Cleanup
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    
    return 0;
}
