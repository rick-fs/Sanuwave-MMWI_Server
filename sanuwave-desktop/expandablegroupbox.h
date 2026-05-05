// expandablegroupbox.h
#ifndef EXPANDABLEGROUPBOX_H
#define EXPANDABLEGROUPBOX_H

#include <QWidget>
#include <QToolButton>
#include <QFrame>
#include <QParallelAnimationGroup>

class QLabel;

/**
 * @brief An expandable/collapsible group box with a disclosure triangle
 * 
 * This widget provides a title bar with a disclosure control and a content
 * area that can be collapsed or expanded. Use it as a drop-in replacement
 * for QGroupBox by adding your layout to the contentWidget().
 * 
 * Example usage:
 *   ExpandableGroupBox* group = new ExpandableGroupBox("Connection", this);
 *   QHBoxLayout* layout = new QHBoxLayout(group->contentWidget());
 *   layout->addWidget(new QLabel("Content"));
 */
class ExpandableGroupBox : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool groupBoxExpanded READ isExpanded WRITE setExpanded NOTIFY expandedChanged)

public:
    /**
     * @brief Position of the disclosure control
     */
    enum DisclosurePosition {
        TopLeft,
        TopRight
    };

    /**
     * @brief Constructor
     * @param title The title of the group box
     * @param parent Parent widget
     */
    explicit ExpandableGroupBox(const QString &title = QString(), QWidget* parent = nullptr);

    /**
     * @brief Constructor with disclosure position
     * @param title The title of the group box
     * @param position Position of the disclosure control
     * @param parent Parent widget
     */
    explicit ExpandableGroupBox(const QString &title, DisclosurePosition position, QWidget* parent = nullptr);

    /**
     * @brief Destructor
     */
    ~ExpandableGroupBox() override;

    /**
     * @brief Get the content widget where you should add your layout/widgets
     * @return The content widget (never null)
     * 
     * Example:
     *   QHBoxLayout* layout = new QHBoxLayout(groupBox->contentWidget());
     *   layout->addWidget(myWidget);
     */
    QWidget* getContentWidget() const;

    /**
     * @brief Set the title text
     * @param title The new title
     */
    void setTitle(const QString &title);

    /**
     * @brief Get the title text
     * @return The current title
     */
    QString title() const;

    /**
     * @brief Check if the group box is expanded
     * @return true if expanded, false if collapsed
     */
    bool isExpanded() const;

    /**
     * @brief Set the expanded state
     * @param expanded true to expand, false to collapse
     */
    void setExpanded(bool expanded);

    /**
     * @brief Set the disclosure control position
     * @param position The desired position
     */
    void setDisclosurePosition(DisclosurePosition position);

    /**
     * @brief Get the current disclosure control position
     * @return The current position
     */
    DisclosurePosition getDisclosurePosition() const;

    /**
     * @brief Set the animation duration in milliseconds
     * @param duration Duration in milliseconds (default is 200)
     */
    void setAnimationDuration(int duration);

    /**
     * @brief Get the animation duration
     * @return Duration in milliseconds
     */
    int getAnimationDuration() const;

    /**
     * @brief Override size hint to return appropriate size based on expanded state
     */
    QSize sizeHint() const override;

    /**
     * @brief Override minimum size hint
     */
    QSize minimumSizeHint() const override;

signals:
    /**
     * @brief Emitted when the expanded state changes
     * @param expanded The new expanded state
     */
    void expandedChanged(bool expanded);

public slots:
    /**
     * @brief Toggle the expanded state
     */
    void toggle();

    /**
     * @brief Expand the group box
     */
    void expand();

    /**
     * @brief Collapse the group box
     */
    void collapse();

private slots:
    /**
     * @brief Handle disclosure button click
     */
    void onToggleClicked();

    /**
     * @brief Handle animation finished
     */
    void onAnimationFinished();

private:
    /**
     * @brief Initialize the widget
     */
    void initialize();

    /**
     * @brief Update the disclosure button icon
     */
    void updateDisclosureIcon();

    /**
     * @brief Calculate the header height
     */
    int getHeaderHeight() const;

private:
    QToolButton* disclosureButton;
    QLabel* titleLabel;
    QWidget* headerWidget;
    QWidget* contentWidget;
    QFrame* mainFrame;
    
    DisclosurePosition disclosurePosition;
    bool groupBoxExpanded;
    int animationDuration;
    int collapsedHeight;
    int expandedHeight;
    QParallelAnimationGroup* animationGroup;
};

#endif // EXPANDABLEGROUPBOX_H