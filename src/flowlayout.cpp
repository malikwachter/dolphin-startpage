#include "flowlayout.h"
#include <QWidget>

FlowLayout::FlowLayout(QWidget *parent, int spacing) : QLayout(parent), m_spacing(spacing)
{
    setContentsMargins(0, 0, 0, 0);
}
FlowLayout::~FlowLayout() { clear(); }

void FlowLayout::addItem(QLayoutItem *item) { m_items.append(item); }

void FlowLayout::clear()
{
    while (QLayoutItem *item = takeAt(0)) {
        if (QWidget *w = item->widget()) w->deleteLater();
        delete item;
    }
}

QSize FlowLayout::minimumSize() const
{
    QSize s;
    for (QLayoutItem *i : m_items) s = s.expandedTo(i->minimumSize());
    const QMargins m = contentsMargins();
    return s + QSize(m.left() + m.right(), m.top() + m.bottom());
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
    const QMargins m = contentsMargins();
    const QRect r = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = r.x(), y = r.y(), lineHeight = 0;
    for (QLayoutItem *item : m_items) {
        if (item->widget() && item->widget()->isHidden()) continue;
        const QSize sz = item->sizeHint();
        if (x + sz.width() > r.right() + 1 && lineHeight > 0) {
            x = r.x();
            y += lineHeight + m_spacing;
            lineHeight = 0;
        }
        if (!testOnly) item->setGeometry(QRect(QPoint(x, y), sz));
        x += sz.width() + m_spacing;
        lineHeight = qMax(lineHeight, sz.height());
    }
    return y + lineHeight - rect.y() + m.bottom();
}
