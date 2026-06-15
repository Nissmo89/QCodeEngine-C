#include <QtTest>

#include "treesitterhelper.h"

namespace {

const DocumentSymbol* findSymbol(const QVector<DocumentSymbol>& symbols,
                                 const QString& name,
                                 DocumentSymbolKind kind)
{
    for (const DocumentSymbol& symbol : symbols) {
        if (symbol.kind == kind && symbol.name == name)
            return &symbol;
    }
    return nullptr;
}

int countSymbols(const QVector<DocumentSymbol>& symbols,
                 const QString& name,
                 DocumentSymbolKind kind)
{
    int count = 0;
    for (const DocumentSymbol& symbol : symbols) {
        if (symbol.kind == kind && symbol.name == name)
            ++count;
    }
    return count;
}

} // namespace

class TestDocumentSymbols : public QObject
{
    Q_OBJECT

private slots:
    void extractsOutlineSymbolsWithKindsAndDisplayText();
    void prefersFunctionDefinitionsOverPrototypes();
};

void TestDocumentSymbols::extractsOutlineSymbolsWithKindsAndDisplayText()
{
    const QString source = QStringLiteral(R"(#define WIDGET_LIMIT 32
#define APPLY_WIDGET(name) apply_widget(name)

typedef unsigned long widget_id_t;

struct WidgetConfig {
    int width;
};

enum WidgetState {
    WidgetStateIdle,
    WidgetStateBusy,
};

static int helper(int value);
)");

    TreeSitterHelper helper(source);
    const QVector<DocumentSymbol> symbols = extractDocumentSymbols(helper.get_m_tree(), source);

    const DocumentSymbol* macro = findSymbol(symbols, QStringLiteral("WIDGET_LIMIT"), DocumentSymbolKind::Macro);
    QVERIFY2(macro, "Expected object-like macro symbol");
    QCOMPARE(macro->displayText, QStringLiteral("#define WIDGET_LIMIT 32"));

    const DocumentSymbol* macroFunc = findSymbol(symbols, QStringLiteral("APPLY_WIDGET"), DocumentSymbolKind::Macro);
    QVERIFY2(macroFunc, "Expected function-like macro symbol");
    QCOMPARE(macroFunc->displayText, QStringLiteral("#define APPLY_WIDGET(name) apply_widget(name)"));

    const DocumentSymbol* typedefSymbol = findSymbol(symbols, QStringLiteral("widget_id_t"), DocumentSymbolKind::Type);
    QVERIFY2(typedefSymbol, "Expected typedef symbol");
    QCOMPARE(typedefSymbol->displayText, QStringLiteral("typedef unsigned long widget_id_t"));

    const DocumentSymbol* structSymbol = findSymbol(symbols, QStringLiteral("WidgetConfig"), DocumentSymbolKind::Type);
    QVERIFY2(structSymbol, "Expected struct symbol");
    QCOMPARE(structSymbol->displayText, QStringLiteral("struct WidgetConfig"));

    const DocumentSymbol* fieldSymbol = findSymbol(symbols, QStringLiteral("width"), DocumentSymbolKind::Field);
    QVERIFY2(fieldSymbol, "Expected struct field symbol");
    QCOMPARE(fieldSymbol->displayText, QStringLiteral("int width"));

    const DocumentSymbol* enumSymbol = findSymbol(symbols, QStringLiteral("WidgetState"), DocumentSymbolKind::Type);
    QVERIFY2(enumSymbol, "Expected enum symbol");
    QCOMPARE(enumSymbol->displayText, QStringLiteral("enum WidgetState"));

    const DocumentSymbol* enumeratorSymbol =
        findSymbol(symbols, QStringLiteral("WidgetStateBusy"), DocumentSymbolKind::EnumConstant);
    QVERIFY2(enumeratorSymbol, "Expected enum constant symbol");
    QCOMPARE(enumeratorSymbol->displayText, QStringLiteral("WidgetStateBusy"));

    const DocumentSymbol* functionSymbol =
        findSymbol(symbols, QStringLiteral("helper"), DocumentSymbolKind::Function);
    QVERIFY2(functionSymbol, "Expected function prototype symbol");
    QCOMPARE(functionSymbol->displayText, QStringLiteral("static int helper(int value)"));
    QVERIFY(!functionSymbol->isDefinition);
}

void TestDocumentSymbols::prefersFunctionDefinitionsOverPrototypes()
{
    const QString source = QStringLiteral(R"(static int compute(int value);

static int compute(int value)
{
    return value + 1;
}
)");

    TreeSitterHelper helper(source);
    const QVector<DocumentSymbol> symbols = extractDocumentSymbols(helper.get_m_tree(), source);

    QCOMPARE(countSymbols(symbols, QStringLiteral("compute"), DocumentSymbolKind::Function), 1);

    const DocumentSymbol* functionSymbol =
        findSymbol(symbols, QStringLiteral("compute"), DocumentSymbolKind::Function);
    QVERIFY2(functionSymbol, "Expected function symbol");
    QVERIFY(functionSymbol->isDefinition);
    QCOMPARE(functionSymbol->displayText, QStringLiteral("static int compute(int value)"));
}

QTEST_MAIN(TestDocumentSymbols)
#include "test_document_symbols.moc"
