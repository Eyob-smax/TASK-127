#pragma once
// CommandPaletteDialog.h — ProctorOps
// Ctrl+K command palette — keyboard-only fuzzy search over registered actions.
//
// Opens as a popup dialog with a filter line edit and a keyboard-navigable list.
// Up/Down arrows navigate the list; Enter dispatches the selected action;
// Escape dismisses without action. Mouse double-click also dispatches.

#include <QDialog>

class QLineEdit;
class QListWidget;
class ActionRouter;

class CommandPaletteDialog : public QDialog {
    Q_OBJECT

public:
    explicit CommandPaletteDialog(ActionRouter& router, QWidget* parent = nullptr);

    /// Show the palette, reset the filter, select the first item, and focus the input.
    void activate();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onFilterChanged(const QString& text);
    void onItemDoubleClicked();

private:
    void rebuildList(const QString& filter);
    void dispatchSelected();

    ActionRouter& m_router;
    QLineEdit*    m_filterEdit{nullptr};
    QListWidget*  m_listWidget{nullptr};
};
