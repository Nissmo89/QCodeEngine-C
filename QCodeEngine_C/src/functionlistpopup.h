#ifndef FLOATINGLISTPOPUP_H
#define FLOATINGLISTPOPUP_H

#include <QWidget>
#include <QListWidget>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include "CodeEditor/EditorTheme.h"

class FunctionListItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    FunctionListItemDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void setTheme(const QEditorTheme& theme) {
        m_theme = theme;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        // Background
        QRect itemRect = option.rect;
        itemRect.adjust(2, 2, -2, -2);
        
        if (option.state & QStyle::State_Selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_theme.selectionBackground);
            painter->drawRoundedRect(itemRect, 4, 4);
            
            // subtle accent using tokenKeyword or a generic accent
            painter->setBrush(m_theme.tokenKeyword);
            painter->drawRoundedRect(itemRect.adjusted(0, 0, -itemRect.width() + 3, 0), 4, 4);
        } else if (option.state & QStyle::State_MouseOver) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_theme.currentLineBackground);
            painter->drawRoundedRect(itemRect, 4, 4);
        }

        QString signature = index.data(Qt::DisplayRole).toString();

        QRect textRect = option.rect.adjusted(12, 0, -12, 0);
        QFont font(m_theme.fontFamily, m_theme.fontSize - 1);
        font.setFixedPitch(true);
        font.setStyleHint(QFont::Monospace);
        
        int parenIdx = signature.indexOf('(');
        if (parenIdx != -1) {
            int lastSpace = signature.lastIndexOf(' ', parenIdx);
            
            QString returnType = "";
            QString funcName = "";
            
            if (lastSpace != -1) {
                returnType = signature.left(lastSpace + 1);
                funcName = signature.mid(lastSpace + 1, parenIdx - lastSpace - 1);
            } else {
                funcName = signature.left(parenIdx);
            }
            
            QString argsPart = signature.mid(parenIdx);

            int xOffset = textRect.x();
            int remainingWidth = textRect.width();
            
            auto drawTextPart = [&](const QString& text, const QColor& color, bool isBold = false) {
                if (remainingWidth <= 0 || text.isEmpty()) return;
                painter->setPen(color);
                QFont f = font;
                f.setBold(isBold);
                painter->setFont(f);
                QString elided = painter->fontMetrics().elidedText(text, Qt::ElideRight, qMax(0, remainingWidth));
                painter->drawText(QRect(xOffset, textRect.y(), remainingWidth, textRect.height()), Qt::AlignLeft | Qt::AlignVCenter, elided);
                int advance = painter->fontMetrics().horizontalAdvance(elided); // Use elided width to correctly track position bounded by remaining width
                if (elided.endsWith("...")) {
                    advance = painter->fontMetrics().horizontalAdvance(elided);
                } else {
                    advance = painter->fontMetrics().horizontalAdvance(text);
                }
                xOffset += advance;
                remainingWidth -= advance;
            };

            auto isAttrBeforeIdentifier = [&](const QString& part, int wordEndIndex) {
                 for (int i = wordEndIndex; i < part.length(); ++i) {
                     QChar c = part[i];
                     if (c == ' ' || c == '\t' || c == '*' || c == '&') continue;
                     if (c == ',' || c == ')' || c == '[') return true; 
                     if (c.isLetterOrNumber() || c == '_') return false; 
                 }
                 return true; 
            };

            auto drawSyntax = [&](const QString& part, bool isArgs) {
                QString word;
                static const QStringList keywords = {"const", "struct", "enum", "union", "unsigned", "signed", "long", "short", "static", "volatile", "inline", "extern", "restrict"};
                static const QStringList primitives = {"int", "float", "double", "char", "void", "bool", "size_t", "ssize_t", "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t", "int64_t", "uint64_t"};

                auto flushWord = [&](int endIndex) {
                    if (word.isEmpty()) return;
                    if (keywords.contains(word)) drawTextPart(word, m_theme.tokenKeyword, m_theme.keywordBold);
                    else if (primitives.contains(word)) drawTextPart(word, m_theme.tokenType, m_theme.typeBold);
                    else if (word.toUpper() == word && word.length() > 1 && !word.contains(QRegularExpression("[0-9]"))) drawTextPart(word, m_theme.tokenConstant); 
                    else if (word.endsWith("_t")) drawTextPart(word, m_theme.tokenType, m_theme.typeBold);
                    else if (word.length() > 0 && word[0].isUpper()) drawTextPart(word, m_theme.tokenType, m_theme.typeBold);
                    else {
                        if (isArgs) {
                            if (isAttrBeforeIdentifier(part, endIndex)) drawTextPart(word, m_theme.tokenIdentifier);
                            else drawTextPart(word, m_theme.tokenType, m_theme.typeBold);
                        } else {
                            drawTextPart(word, m_theme.tokenType, m_theme.typeBold);
                        }
                    }
                    word.clear();
                };

                for (int i = 0; i < part.length(); ++i) {
                    QChar c = part[i];
                    if (c.isLetterOrNumber() || c == '_') {
                        word += c;
                    } else {
                        flushWord(i);
                        
                        if (c == ' ' || c == '\t') {
                            drawTextPart(QString(c), m_theme.foreground);
                        } else if (c == '*' || c == '&' || c == '(' || c == ')' || c == ',' || c == '[' || c == ']') {
                            // Commas and brackets can be treated as punctuation
                            drawTextPart(QString(c), m_theme.tokenPunctuation.isValid() ? m_theme.tokenPunctuation : m_theme.foreground);
                        } else {
                            drawTextPart(QString(c), m_theme.foreground);
                        }
                    }
                }
                flushWord(part.length());
            };
            
            // 1. Return Type
            if (!returnType.isEmpty()) {
                drawSyntax(returnType, false);
            }
            
            // 2. Function Name
            if (!funcName.isEmpty() && remainingWidth > 0) {
                drawTextPart(funcName, m_theme.tokenFunction, m_theme.functionBold);
            }

            // 3. Arguments
            if (!argsPart.isEmpty() && remainingWidth > 0) {
                drawSyntax(argsPart, true);
            }
            
        } else {
            painter->setFont(font);
            painter->setPen(m_theme.tokenFunction);
            QString elidedSig = painter->fontMetrics().elidedText(signature, Qt::ElideRight, textRect.width());
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedSig);
        }

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(32);
        return s;
    }

private:
    QEditorTheme m_theme;
};

class FloatingListPopup : public QWidget
{
    Q_OBJECT

public:
    explicit FloatingListPopup(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        /* ---------- WINDOW SETUP ---------- */
        setObjectName("FloatingListPopup");
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Popup);
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::StrongFocus);

        /* ---------- LIST ---------- */
        list = new QListWidget(this);
        list->setObjectName("FloatingListPopup_List");
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setUniformItemSizes(true);
        list->setMouseTracking(true);
        list->setFocusPolicy(Qt::StrongFocus);
        list->setFrameShape(QFrame::NoFrame);
        list->setStyleSheet("background: transparent; outline: none;");
        
        m_delegate = new FunctionListItemDelegate(list);
        list->setItemDelegate(m_delegate);

        /* ---------- LAYOUT ---------- */
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(0);
        layout->addWidget(list);
        setLayout(layout);

        /* ---------- INTERNAL QSS ---------- */
        setStyleSheet(R"(
            QScrollBar:vertical {
                width: 8px;
                background: transparent;
                margin: 2px;
            }
            QScrollBar::handle:vertical {
                background: rgba(150, 150, 150, 100);
                border-radius: 4px;
                min-height: 20px;
            }
            QScrollBar::handle:vertical:hover {
                background: rgba(150, 150, 150, 180);
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
                height: 0px;
            }
        )");

        connect(list, &QListWidget::itemClicked,
                this, &FloatingListPopup::activateCurrentItem);

        // Extra width for signatures
        setFixedWidth(650);
        setMaximumHeight(500);
        setFocusProxy(list);
    }

    /* ---------- API ---------- */
    void clear() { list->clear(); }

    void setTheme(const QEditorTheme &theme) {
        m_theme = theme;
        if (m_delegate) {
            m_delegate->setTheme(theme);
        }
        update(); // repaints background
        list->viewport()->update(); // Force items to repaint
    }

    void addFunction(const QString &name, int line)
    {
        auto *item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, line);
        list->addItem(item);
    }

    void showBelowWidget(QWidget *anchor, int yOffset = 20)
    {
        if (!anchor) return;

        const int rowHeight = list->sizeHintForRow(0) > 0 ? list->sizeHintForRow(0) : 32;
        const int rows = list->count();
        const int margins = layout()->contentsMargins().top()
                            + layout()->contentsMargins().bottom();

        int desiredHeight = rows * rowHeight + margins;
        desiredHeight = qMin(desiredHeight, maximumHeight());
        desiredHeight = qMax(desiredHeight, 60);

        resize(width(), desiredHeight);

        QPoint anchorPos = anchor->mapToGlobal(QPoint(0, 0));
        
        // Position at the top-center of the editor, similar to VS Code command palette
        QPoint pos(
            anchorPos.x() + anchor->width() / 2 - width() / 2,
            anchorPos.y() + yOffset
            );

        move(pos);
        list->setCurrentRow(0);

        show();
        raise();
        list->setFocus();
        list->setCurrentRow(0);
    }

    bool isEmpty() const {
        return list->count() == 0;
    }

protected:
    /* ---------- PERFECT BACKGROUND ---------- */
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // We use 6px of the transparent widget bounds for the drop shadow.
        // The list is positioned inside layout margins of 10.
        // So the actual visible solid window is inset by 6px
        QRectF r = rect().adjusted(6, 6, -6, -6);
        QPainterPath path;
        path.addRoundedRect(r, 6, 6);

        // Draw soft drop shadow downwards
        for(int i = 1; i <= 6; i++) {
            QColor c(0, 0, 0, 40 - (i * 6));
            p.setPen(QPen(c, 1));
            p.setBrush(Qt::NoBrush);
            // Shift shadow slightly down (+2 Y)
            p.drawRoundedRect(r.adjusted(-i, -i + 2, i, i + 2), 6, 6);
        }

        // Generate a contrasting background color
        QColor bg = m_theme.background;
        if (bg.isValid()) {
            if (bg.lightness() < 128) {
                bg = bg.lighter(115); // Slightly lighter for dark themes
            } else {
                bg = bg.darker(105);  // Slightly darker for light themes
            }
        } else {
            bg = QColor(30, 30, 30);
        }
        bg.setAlpha(250);

        p.fillPath(path, bg);
        
        QColor border = m_theme.gutterBorderColor;
        p.setPen(QPen(border.isValid() ? border : QColor(60, 60, 60), 1));
        p.drawPath(path);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            hide();
            return;
        }

        if (event->key() == Qt::Key_Return ||
            event->key() == Qt::Key_Enter) {
            activateCurrentItem();
            return;
        }

        QWidget::keyPressEvent(event);
    }

private slots:
    void activateCurrentItem()
    {
        auto *item = list->currentItem();
        if (!item)
            return;

        emit functionSelected(item->data(Qt::UserRole).toInt());
        hide();
    }

signals:
    void functionSelected(int line);

private:
    QListWidget *list = nullptr;
    FunctionListItemDelegate *m_delegate = nullptr;
    QEditorTheme m_theme;
};

#endif // FLOATINGLISTPOPUP_H
