// expandablegroupbox.cpp
#include "expandablegroupbox.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QScrollArea>

ExpandableGroupBox::ExpandableGroupBox(const QString &title, QWidget *parent)
    : QWidget(parent), disclosureButton(nullptr), titleLabel(nullptr), headerWidget(nullptr)
    , contentWidget(nullptr), mainFrame(nullptr), disclosurePosition(TopLeft), groupBoxExpanded(false)
    , animationDuration(200), collapsedHeight(0), expandedHeight(0), animationGroup(nullptr)
{
    initialize();
    setTitle(title);
}

ExpandableGroupBox::ExpandableGroupBox(const QString &title, DisclosurePosition position, QWidget *parent)
    : QWidget(parent), disclosureButton(nullptr), titleLabel(nullptr), headerWidget(nullptr)
    , contentWidget(nullptr), mainFrame(nullptr), disclosurePosition(position), groupBoxExpanded(false)
    , animationDuration(200), collapsedHeight(0), expandedHeight(0), animationGroup(nullptr)
{
    initialize();
    setTitle(title);
}

ExpandableGroupBox::~ExpandableGroupBox()
{
    // QObject parent-child relationship handles cleanup
}

void ExpandableGroupBox::initialize()
{
    // Set size policy for the ExpandableGroupBox itself
    // This tells the parent layout to only give us the vertical space we actually need
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    // Main layout for this widget
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Create the main frame (similar to QGroupBox appearance)
    mainFrame = new QFrame(this);
    mainFrame->setFrameShape(QFrame::StyledPanel);
    mainFrame->setFrameShadow(QFrame::Plain);
    mainLayout->addWidget(mainFrame);

    QVBoxLayout *frameLayout = new QVBoxLayout(mainFrame);
    frameLayout->setContentsMargins(2, 2, 2, 2);
    frameLayout->setSpacing(5);

    // Create header widget (title bar)
    headerWidget = new QWidget(mainFrame);
    QHBoxLayout *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(5, 5, 5, 5);
    headerLayout->setSpacing(5);

    // Create disclosure button
    disclosureButton = new QToolButton(headerWidget);
    disclosureButton->setStyleSheet("QToolButton { border: none; }");
    disclosureButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    disclosureButton->setArrowType(Qt::DownArrow);
    disclosureButton->setCheckable(true);
    disclosureButton->setChecked(false);
    disclosureButton->setCursor(Qt::PointingHandCursor);
    disclosureButton->setFixedSize(16, 16);
    connect(disclosureButton, &QToolButton::clicked, this, &ExpandableGroupBox::onToggleClicked);

    // Create title label
    titleLabel = new QLabel(headerWidget);
    QFont font = titleLabel->font();
    font.setBold(true);
    titleLabel->setFont(font);

    // Arrange header based on disclosure position
    if (disclosurePosition == TopLeft)
    {
        headerLayout->addWidget(disclosureButton);
        headerLayout->addWidget(titleLabel);
    }
    else
    { // TopRight
        headerLayout->addWidget(titleLabel);
        headerLayout->addStretch();
        headerLayout->addWidget(disclosureButton);
    }

    if (disclosurePosition == TopLeft)
    {
        headerLayout->addStretch();
    }

    frameLayout->addWidget(headerWidget);

    // Create content widget (this is where user adds their widgets)
    contentWidget = new QWidget(mainFrame);

    // Set size policy to prevent unwanted resizing during animation
    contentWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    contentWidget->setMaximumHeight(0);  
    contentWidget->hide();   
    frameLayout->addWidget(contentWidget);

    // Prevent the frame from expanding beyond necessary
    mainFrame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    // Setup animation
    animationGroup = new QParallelAnimationGroup(this);

    QPropertyAnimation *contentAnim = new QPropertyAnimation(contentWidget, "maximumHeight", this);
    contentAnim->setDuration(animationDuration);
    contentAnim->setEasingCurve(QEasingCurve::InOutQuad);
    animationGroup->addAnimation(contentAnim);

    connect(animationGroup, &QParallelAnimationGroup::finished,
            this, &ExpandableGroupBox::onAnimationFinished);

    updateGeometry();
    updateDisclosureIcon();
}

QWidget *ExpandableGroupBox::getContentWidget() const
{
    return contentWidget;
}

void ExpandableGroupBox::setTitle(const QString &title)
{
    if (titleLabel)
    {
        titleLabel->setText(title);
    }
}

QString ExpandableGroupBox::title() const
{
    return titleLabel ? titleLabel->text() : QString();
}

void ExpandableGroupBox::updateDisclosureIcon()
{
    if (!disclosureButton)
    {
        return;
    }

    if (groupBoxExpanded)
    {
        disclosureButton->setArrowType(Qt::DownArrow);
    }
    else
    {
        disclosureButton->setArrowType(Qt::RightArrow);
    }
}

void ExpandableGroupBox::onToggleClicked()
{
    toggle();
}

void ExpandableGroupBox::onAnimationFinished()
{
    if (!groupBoxExpanded)
    {
        contentWidget->setMaximumHeight(0);
        contentWidget->setMinimumHeight(0);
        contentWidget->hide();
    }
    else
    {
        contentWidget->setMaximumHeight(QWIDGETSIZE_MAX);
        contentWidget->setMinimumHeight(0);
    }

    // Force layout update after animation completes
    contentWidget->updateGeometry();
    updateGeometry();
    
    // Notify parent widget/layout that our size has changed
    if (parentWidget())
    {
        parentWidget()->updateGeometry();
    }
}

bool ExpandableGroupBox::isExpanded() const
{
    return groupBoxExpanded;
}

void ExpandableGroupBox::setExpanded(bool expanded)
{
    if (isExpanded() == expanded)
    {
        return;
    }

    groupBoxExpanded = expanded;
    disclosureButton->setChecked(expanded);
    updateDisclosureIcon();

    // Stop any running animation
    if (animationGroup->state() == QAbstractAnimation::Running)
    {
        animationGroup->stop();
    }

    QPropertyAnimation *contentAnim = static_cast<QPropertyAnimation *>(
        animationGroup->animationAt(0));

    if (expanded)
    {
        // Expanding
        contentWidget->show();
        contentWidget->setMaximumHeight(0);
        contentWidget->setMinimumHeight(0);

        // Calculate target height
        int targetHeight = contentWidget->sizeHint().height();

        contentAnim->setStartValue(0);
        contentAnim->setEndValue(targetHeight);
        animationGroup->start();
    }
    else
    {
        // Collapsing
        int startHeight = contentWidget->height();

        contentAnim->setStartValue(startHeight);
        contentAnim->setEndValue(0);
        animationGroup->start();
    }

    emit expandedChanged(expanded);

    // Notify parent layout that our size requirements changed
    updateGeometry();
    if (parentWidget())
    {
        parentWidget()->updateGeometry();
    }
}

void ExpandableGroupBox::toggle()
{
    setExpanded(!groupBoxExpanded);
}

void ExpandableGroupBox::expand()
{
    setExpanded(true);
}

void ExpandableGroupBox::collapse()
{
    setExpanded(false);
}

void ExpandableGroupBox::setDisclosurePosition(DisclosurePosition position)
{
    if (disclosurePosition == position)
    {
        return;
    }

    disclosurePosition = position;
}

ExpandableGroupBox::DisclosurePosition ExpandableGroupBox::getDisclosurePosition() const
{
    return disclosurePosition;
}

void ExpandableGroupBox::setAnimationDuration(int duration)
{
    animationDuration = duration;
    for (int i = 0; i < animationGroup->animationCount(); ++i)
    {
        QPropertyAnimation *anim = qobject_cast<QPropertyAnimation *>(animationGroup->animationAt(i));
        if (anim)
        {
            anim->setDuration(duration);
        }
    }
}

int ExpandableGroupBox::getAnimationDuration() const
{
    return animationDuration;
}

int ExpandableGroupBox::getHeaderHeight() const
{
    if (!headerWidget)
    {
        return 0;
    }
    
    // Return the header's size hint plus frame margins
    QVBoxLayout* frameLayout = qobject_cast<QVBoxLayout*>(mainFrame->layout());
    int margins = 0;
    if (frameLayout)
    {
        margins = frameLayout->contentsMargins().top() + 
                  frameLayout->contentsMargins().bottom() +
                  frameLayout->spacing();
    }
    
    // Add frame line width
    margins += mainFrame->lineWidth() * 2;
    
    return headerWidget->sizeHint().height() + margins;
}

QSize ExpandableGroupBox::sizeHint() const
{
    if (!mainFrame)
    {
        return QWidget::sizeHint();
    }
    
    int width = mainFrame->sizeHint().width();
    int height;
    
    if (groupBoxExpanded)
    {
        // When expanded, return header + content height
        height = getHeaderHeight() + contentWidget->sizeHint().height();
    }
    else
    {
        // When collapsed, return only header height
        height = getHeaderHeight();
    }
    
    return QSize(width, height);
}

QSize ExpandableGroupBox::minimumSizeHint() const
{
    if (!mainFrame)
    {
        return QWidget::minimumSizeHint();
    }
    
    int width = mainFrame->minimumSizeHint().width();
    int height;
    
    if (groupBoxExpanded)
    {
        // When expanded, minimum is header + minimum content
        height = getHeaderHeight() + contentWidget->minimumSizeHint().height();
    }
    else
    {
        // When collapsed, minimum is just the header
        height = getHeaderHeight();
    }
    
    return QSize(width, height);
}