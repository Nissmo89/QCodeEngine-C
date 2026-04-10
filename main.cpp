#include <QApplication>
#include <QDebug>
#include <CodeEditor/CodeEditor.h>
#include <CodeEditor/EditorTheme.h>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    CodeEditor* editor = new CodeEditor();

    // Use One Dark theme (Zed-style)
    editor->setTheme(QEditorTheme::oneDarkTheme());

    // Configure features
    editor->setLineNumbersVisible(true);
    editor->setFoldingEnabled(true);
    editor->setAutoCompleteEnabled(true);
    editor->setTabWidth(4);
    editor->setInsertSpacesOnTab(true);

    // Load a sample C program to demonstrate the editor
    editor->setText(
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "\n"
        "#define MAX_SIZE 1024\n"
        "\n"
        "typedef struct Node {\n"
        "    int value;\n"
        "    struct Node* next;\n"
        "} Node;\n"
        "\n"
        "// Create a new node\n"
        "Node* create_node(int val) {\n"
        "    Node* node = (Node*)malloc(sizeof(Node));\n"
        "    if (!node) return NULL;\n"
        "    node->value = val;\n"
        "    node->next  = NULL;\n"
        "    return node;\n"
        "}\n"
        "\n"
        "/* Insert at head of list */\n"
        "void push(Node** head, int val) {\n"
        "    Node* n = create_node(val);\n"
        "    if (!n) return;\n"
        "    n->next = *head;\n"
        "    *head = n;\n"
        "}\n"
        "\n"
        "int sum_list(Node* head) {\n"
        "    int total = 0;\n"
        "    for (Node* cur = head; cur != NULL; cur = cur->next) {\n"
        "        total += cur->value;\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "\n"
        "int main(void) {\n"
        "    Node* list = NULL;\n"
        "    for (int i = 0; i < 10; i++) {\n"
        "        push(&list, i * i);\n"
        "    }\n"
        "    printf(\"Sum = %d\\n\", sum_list(list));\n"
        "    return 0;\n"
        "}\n"
    );

    editor->resize(1000, 720);
    editor->setWindowTitle("QCodeEditor - One Dark");
    editor->show();

    return app.exec();
}
