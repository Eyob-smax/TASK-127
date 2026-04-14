#pragma once
// QuestionBankWindow.h — ProctorOps
// Question bank browser — query, filter, and manage exam questions.
//
// Layout:
//   Top bar — toolbar (New, Edit, Delete, Refresh) + filter toggle button
//   Filter panel (collapsible) — difficulty range, KP subtree, text search, status
//   QTableView — question list from QuestionService::queryQuestions()
//   Status bar row — row count, filter summary
//
// Double-click or Edit action opens QuestionEditorDialog.
// Right-click context menu: Edit, Delete, Manage KP Mappings, Manage Tags.
// Pagination via Load More button when result count == filter.limit.

#include <QWidget>
#include "models/Question.h"

class QTableView;
class QStandardItemModel;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QPushButton;
class QLabel;
class QGroupBox;
struct AppContext;

class QuestionBankWindow : public QWidget {
    Q_OBJECT

public:
    explicit QuestionBankWindow(AppContext& ctx, QWidget* parent = nullptr);

    static constexpr const char* WindowId = "window.question_bank";

public slots:
    /// Invoked by MainShell router action "content.map_to_kp".
    Q_INVOKABLE void mapSelectedToKnowledgePoint();

    /// Invoked by MainShell router action for advanced filter delegation.
    Q_INVOKABLE void activateFilter();

private slots:
    void onNewQuestion();
    void onEditQuestion();
    void onDeleteQuestion();
    void onRefresh();
    void onTableDoubleClicked(const QModelIndex& index);
    void onContextMenu(const QPoint& pos);
    void onLoadMore();
    void onFilterChanged();

private:
    void setupUi();
    void setupFilterPanel();
    void setupToolbar();
    void buildQuestionFilter(QuestionFilter& filter) const;
    void loadQuestions(bool append = false);
    void populateTable(const QList<Question>& questions, bool append);
    QString selectedQuestionId() const;
    void openEditor(const QString& questionId = {});
    void deleteSelected();

    AppContext&          m_ctx;
    QList<Question>      m_questions;
    int                  m_currentOffset{0};

    // Filter widgets
    QGroupBox*           m_filterBox{nullptr};
    QSpinBox*            m_diffMinSpin{nullptr};
    QSpinBox*            m_diffMaxSpin{nullptr};
    QDoubleSpinBox*      m_discMinSpin{nullptr};   // discrimination lower bound (0.00–1.00)
    QDoubleSpinBox*      m_discMaxSpin{nullptr};   // discrimination upper bound (0.00–1.00)
    QLineEdit*           m_textSearchEdit{nullptr};
    QComboBox*           m_statusCombo{nullptr};
    QComboBox*           m_kpCombo{nullptr};       // populated from QuestionService::getTree()
    QComboBox*           m_tagCombo{nullptr};      // populated from QuestionService::listTags()

    // Table
    QTableView*          m_tableView{nullptr};
    QStandardItemModel*  m_model{nullptr};

    // Footer
    QLabel*              m_statusLabel{nullptr};
    QPushButton*         m_loadMoreBtn{nullptr};
};
