#pragma once
#include <QLayout>
#include <QList>

class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent = nullptr, int spacing = 8);
    ~FlowLayout() override;
    void addItem(QLayoutItem *item) override;
    int count() const override { return m_items.size(); }
    QLayoutItem *itemAt(int i) const override { return m_items.value(i); }
    QLayoutItem *takeAt(int i) override { return (i >= 0 && i < m_items.size()) ? m_items.takeAt(i) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return doLayout(QRect(0, 0, w, 0), true); }
    QSize minimumSize() const override;
    QSize sizeHint() const override { return minimumSize(); }
    void setGeometry(const QRect &r) override { QLayout::setGeometry(r); doLayout(r, false); }
    void clear();
private:
    int doLayout(const QRect &rect, bool testOnly) const;
    QList<QLayoutItem *> m_items;
    int m_spacing;
};
