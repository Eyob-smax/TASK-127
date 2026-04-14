// MaskedFieldWidget.cpp — ProctorOps

#include "widgets/MaskedFieldWidget.h"
#include "utils/ClipboardGuard.h"
#include "utils/Validation.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

MaskedFieldWidget::MaskedFieldWidget(QWidget* parent)
    : QWidget(parent)
{
    m_label = new QLabel(this);
    m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_copyBtn   = new QPushButton(tr("Copy"), this);
    m_revealBtn = new QPushButton(tr("Reveal"), this);

    m_copyBtn->setToolTip(tr("Copy masked value to clipboard"));
    m_revealBtn->setToolTip(tr("Temporarily reveal full value (requires step-up)"));
    m_copyBtn->setMaximumWidth(56);
    m_revealBtn->setMaximumWidth(64);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(m_label, 1);
    layout->addWidget(m_copyBtn);
    layout->addWidget(m_revealBtn);

    m_revealTimer.setSingleShot(true);
    m_revealTimer.setInterval(Validation::StepUpWindowSeconds * 1000);

    connect(m_copyBtn,   &QPushButton::clicked, this, &MaskedFieldWidget::onCopyClicked);
    connect(m_revealBtn, &QPushButton::clicked, this, &MaskedFieldWidget::onRevealClicked);
    connect(&m_revealTimer, &QTimer::timeout,   this, &MaskedFieldWidget::onRevealTimeout);

    updateDisplay();
}

void MaskedFieldWidget::setValue(const QString& value)
{
    m_value    = value;
    m_revealed = false;
    m_revealTimer.stop();
    updateDisplay();
}

void MaskedFieldWidget::reveal()
{
    m_revealed = true;
    updateDisplay();
    m_revealTimer.start();
}

void MaskedFieldWidget::remask()
{
    m_revealed = false;
    m_revealTimer.stop();
    updateDisplay();
}

void MaskedFieldWidget::onRevealTimeout()
{
    remask();
}

void MaskedFieldWidget::onCopyClicked()
{
    // Always copies the masked form — never the raw PII
    ClipboardGuard::copyMasked(m_value);
}

void MaskedFieldWidget::onRevealClicked()
{
    if (m_revealed) {
        remask();
    } else {
        emit revealRequested();
    }
}

void MaskedFieldWidget::updateDisplay()
{
    if (m_revealed) {
        m_label->setText(m_value);
        m_revealBtn->setText(tr("Hide"));
    } else {
        m_label->setText(ClipboardGuard::maskValue(m_value));
        m_revealBtn->setText(tr("Reveal"));
    }
}
