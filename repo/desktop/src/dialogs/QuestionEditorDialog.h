#pragma once
// QuestionEditorDialog.h — ProctorOps
// Create or edit a question with all governance fields.
//
// Fields:
//   bodyText     — QTextEdit (max 4000 chars)
//   answerOptions — dynamic list (2–6 entries, add/remove buttons)
//   correctAnswerIndex — QComboBox populated from answer option list
//   difficulty   — QSpinBox 1–5
//   discrimination — QDoubleSpinBox 0.00–1.00
//   status       — QComboBox (Draft / Active / Archived)
//   externalId   — QLineEdit (optional)
//
// Knowledge-point mappings:
//   Current mappings displayed in a list. Add via KP tree combo, remove via button.
//
// Tags:
//   Current tags in a list. Add by name (creates if new), remove via button.
//
// Validation is performed by QuestionService before save. Errors are shown inline.

#include <QDialog>
#include "models/Question.h"

class QTextEdit;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QLabel;
struct AppContext;

class QuestionEditorDialog : public QDialog {
    Q_OBJECT

public:
    /// questionId empty → create mode; non-empty → edit mode.
    explicit QuestionEditorDialog(AppContext& ctx,
                                   const QString& questionId = {},
                                   QWidget* parent = nullptr);

private slots:
    void onSave();
    void onAddAnswer();
    void onRemoveAnswer();
    void onAddKpMapping();
    void onRemoveKpMapping();
    void onAddTag();
    void onRemoveTag();
    void onAnswerListChanged();

private:
    void setupUi();
    void loadQuestion(const QString& questionId);
    void loadKpMappings(const QString& questionId);
    void loadTagMappings(const QString& questionId);
    void loadKpTree();
    void populateCorrectAnswerCombo();
    void showError(const QString& message);

    AppContext&    m_ctx;
    QString        m_questionId;  // empty in create mode
    bool           m_editMode{false};

    // Form fields
    QTextEdit*     m_bodyEdit{nullptr};
    QListWidget*   m_answerList{nullptr};
    QComboBox*     m_correctAnswerCombo{nullptr};
    QSpinBox*      m_difficultySpin{nullptr};
    QDoubleSpinBox* m_discriminationSpin{nullptr};
    QComboBox*     m_statusCombo{nullptr};
    QLineEdit*     m_externalIdEdit{nullptr};

    // KP mappings
    QListWidget*   m_kpMappingList{nullptr};  // item data = kpId
    QComboBox*     m_kpTreeCombo{nullptr};

    // Tags
    QListWidget*   m_tagList{nullptr};        // item data = tagId
    QLineEdit*     m_tagNameEdit{nullptr};

    // Validation feedback
    QLabel*        m_errorLabel{nullptr};

    // Tracking added/removed KP and tag changes for deferred apply after save
    QStringList    m_kpIdsToAdd;
    QStringList    m_kpIdsToRemove;
    QStringList    m_tagIdsToAdd;
    QStringList    m_tagIdsToRemove;
};
