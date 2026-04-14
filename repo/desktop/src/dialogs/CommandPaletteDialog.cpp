// CommandPaletteDialog.cpp — ProctorOps

#include "dialogs/CommandPaletteDialog.h"
#include "app/ActionRouter.h"
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QKeyEvent>

CommandPaletteDialog::CommandPaletteDialog(ActionRouter& router, QWidget* parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint)
    , m_router(router)
{
    setMinimumWidth(500);
    setMinimumHeight(320);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    auto* hint = new QLabel(
        tr("↑↓ navigate  ·  Enter run  ·  Esc dismiss"), this);
    hint->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    layout->addWidget(hint);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Search actions…"));
    m_filterEdit->setClearButtonEnabled(true);
    layout->addWidget(m_filterEdit);

    m_listWidget = new QListWidget(this);
    m_listWidget->setAlternatingRowColors(true);
    m_listWidget->setFocusPolicy(Qt::NoFocus); // keep focus on filter edit
    layout->addWidget(m_listWidget, 1);

    connect(m_filterEdit, &QLineEdit::textChanged,
            this, &CommandPaletteDialog::onFilterChanged);
    connect(m_listWidget, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { onItemDoubleClicked(); });

    rebuildList(QString());
}

void CommandPaletteDialog::activate()
{
    m_filterEdit->clear();
    rebuildList(QString());
    m_filterEdit->setFocus();
    if (m_listWidget->count() > 0)
        m_listWidget->setCurrentRow(0);
    exec();
}

void CommandPaletteDialog::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Up: {
        const int row = m_listWidget->currentRow();
        if (row > 0) m_listWidget->setCurrentRow(row - 1);
        return;
    }
    case Qt::Key_Down: {
        const int row = m_listWidget->currentRow();
        if (row < m_listWidget->count() - 1)
            m_listWidget->setCurrentRow(row + 1);
        return;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
        dispatchSelected();
        return;
    case Qt::Key_Escape:
        reject();
        return;
    default:
        QDialog::keyPressEvent(event);
    }
}

void CommandPaletteDialog::onFilterChanged(const QString& text)
{
    rebuildList(text);
}

void CommandPaletteDialog::onItemDoubleClicked()
{
    dispatchSelected();
}

void CommandPaletteDialog::rebuildList(const QString& filter)
{
    m_listWidget->clear();
    const auto actions = m_router.filter(filter);
    for (const auto& action : actions) {
        auto* item = new QListWidgetItem(m_listWidget);
        const QString shortcutText = action.shortcut.isEmpty()
                                   ? QString()
                                   : QStringLiteral("  [%1]").arg(
                                       action.shortcut.toString(QKeySequence::NativeText));
        item->setText(QStringLiteral("[%1]  %2%3")
                      .arg(action.category, action.displayName, shortcutText));
        item->setData(Qt::UserRole, action.id);
        m_listWidget->addItem(item);
    }
    if (m_listWidget->count() > 0)
        m_listWidget->setCurrentRow(0);
}

void CommandPaletteDialog::dispatchSelected()
{
    const auto* item = m_listWidget->currentItem();
    if (!item) return;
    const QString actionId = item->data(Qt::UserRole).toString();
    accept(); // close the palette before running (avoid focus issues)
    m_router.dispatch(actionId);
}
