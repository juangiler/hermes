#include "pane_tree.h"

#include <QtGlobal>

namespace {

// Leaves are visited left to right, top to bottom: the same order the legacy
// panels_ordered() walks the tree.
void collect_reading_order(const PaneNode *node, QVector<int> &out)
{
  if (!node)
    return;
  if (node->kind == PaneNode::PaneLeaf) {
    out.append(node->paneIndex);
    return;
  }
  collect_reading_order(node->a.get(), out);
  collect_reading_order(node->b.get(), out);
}

PaneNode *find_leaf(PaneNode *node, int pane_index)
{
  if (!node)
    return nullptr;
  if (node->kind == PaneNode::PaneLeaf)
    return node->paneIndex == pane_index ? node : nullptr;
  if (PaneNode *found = find_leaf(node->a.get(), pane_index))
    return found;
  return find_leaf(node->b.get(), pane_index);
}

// panel_remove() focuses node_first_panel(sib) when the focused pane goes.
struct LeafSite {
  PaneNode *leaf = nullptr;
  PaneNode *sibling = nullptr;  // null when the leaf is the whole tree
};

LeafSite find_leaf_site(PaneNode *node, int pane_index)
{
  if (!node)
    return {};
  if (node->kind == PaneNode::PaneLeaf)
    return node->paneIndex == pane_index ? LeafSite{node, nullptr} : LeafSite{};
  if (node->a && node->a->kind == PaneNode::PaneLeaf
      && node->a->paneIndex == pane_index)
    return LeafSite{node->a.get(), node->b.get()};
  if (node->b && node->b->kind == PaneNode::PaneLeaf
      && node->b->paneIndex == pane_index)
    return LeafSite{node->b.get(), node->a.get()};
  LeafSite found = find_leaf_site(node->a.get(), pane_index);
  if (found.leaf)
    return found;
  return find_leaf_site(node->b.get(), pane_index);
}

int first_leaf_index(PaneNode *node)
{
  while (node && node->kind != PaneNode::PaneLeaf)
    node = node->a.get();
  return node ? node->paneIndex : -1;
}

// Removing a leaf collapses its direct parent into the sibling; ancestors keep
// their splits because the recursion stops there. A root leaf leaves no tree.
bool remove_leaf(std::unique_ptr<PaneNode> &slot, int pane_index)
{
  if (!slot)
    return false;
  if (slot->kind == PaneNode::PaneLeaf) {
    if (slot->paneIndex != pane_index)
      return false;
    slot.reset();
    return true;
  }
  if (slot->a && slot->a->kind == PaneNode::PaneLeaf
      && slot->a->paneIndex == pane_index) {
    slot = std::move(slot->b);
    return true;
  }
  if (slot->b && slot->b->kind == PaneNode::PaneLeaf
      && slot->b->paneIndex == pane_index) {
    slot = std::move(slot->a);
    return true;
  }
  return remove_leaf(slot->a, pane_index) || remove_leaf(slot->b, pane_index);
}

// A pane's index is its position in panes(), so the leaves after a closed pane
// have to shift down with it.
void renumber_leaves(PaneNode *node, int removed_index)
{
  if (!node)
    return;
  if (node->kind == PaneNode::PaneLeaf) {
    if (node->paneIndex > removed_index)
      --node->paneIndex;
    return;
  }
  renumber_leaves(node->a.get(), removed_index);
  renumber_leaves(node->b.get(), removed_index);
}

// The divider sits in the single pixel between the children, so the second
// child loses one pixel of extent in the split direction.
void layout_node(const PaneNode *node, const QRect &rect, QVector<QRect> &out)
{
  if (!node)
    return;
  if (node->kind == PaneNode::PaneLeaf) {
    if (node->paneIndex >= 0 && node->paneIndex < out.size())
      out[node->paneIndex] = rect;
    return;
  }
  if (node->kind == PaneNode::HSplit) {
    const int first = qRound(rect.width() * node->ratio);
    layout_node(node->a.get(), QRect(rect.x(), rect.y(), first, rect.height()), out);
    layout_node(node->b.get(), QRect(rect.x() + first + 1, rect.y(),
                                       rect.width() - first - 1, rect.height()), out);
    return;
  }
  const int first = qRound(rect.height() * node->ratio);
  layout_node(node->a.get(), QRect(rect.x(), rect.y(), rect.width(), first), out);
  layout_node(node->b.get(), QRect(rect.x(), rect.y() + first + 1,
                                    rect.width(), rect.height() - first - 1), out);
}

}  // namespace

PaneTree::PaneTree()
{
  m_panes.append(Pane{});
  auto leaf = std::make_unique<PaneNode>();
  leaf->kind = PaneNode::PaneLeaf;
  leaf->paneIndex = 0;
  m_root = std::move(leaf);
}

QVector<Pane> &PaneTree::panes()
{
  return m_panes;
}

const QVector<Pane> &PaneTree::panes() const
{
  return m_panes;
}

PaneNode *PaneTree::root() const
{
  return m_root.get();
}

int PaneTree::focusedPane() const
{
  return m_focused_pane;
}

void PaneTree::setFocusedPane(int index)
{
  if (index >= 0 && index < m_panes.size())
    m_focused_pane = index;
}

int PaneTree::splitPane(int paneIndex, Zone zone, const QString &path)
{
  // Resolve the leaf with the guards first: a refused split must not touch
  // any pane or focus.
  if (paneIndex < 0 || paneIndex >= m_panes.size() || m_panes.size() >= MaxPanes)
    return -1;
  PaneNode *leaf = find_leaf(m_root.get(), paneIndex);
  if (!leaf)
    return -1;

  Pane pane;
  pane.state.path = path;
  const int new_index = m_panes.size();
  m_panes.append(pane);

  auto old_leaf = std::make_unique<PaneNode>();
  old_leaf->kind = PaneNode::PaneLeaf;
  old_leaf->paneIndex = paneIndex;

  auto new_leaf = std::make_unique<PaneNode>();
  new_leaf->kind = PaneNode::PaneLeaf;
  new_leaf->paneIndex = new_index;

  leaf->kind = zone == Zone::Left || zone == Zone::Right
      ? PaneNode::HSplit : PaneNode::VSplit;
  leaf->paneIndex = -1;
  leaf->ratio = 0.5;
  const bool first = zone == Zone::Left || zone == Zone::Top;
  leaf->a = first ? std::move(new_leaf) : std::move(old_leaf);
  leaf->b = first ? std::move(old_leaf) : std::move(new_leaf);
  // panel_split() activates the pane it just made.
  m_focused_pane = new_index;
  return new_index;
}

void PaneTree::closePane(int paneIndex)
{
  if (paneIndex < 0 || paneIndex >= m_panes.size())
    return;

  // Capture the sibling's first pane before the tree gives it the closed
  // leaf's place; its index shifts down with the pane list afterwards.
  const LeafSite site = find_leaf_site(m_root.get(), paneIndex);
  if (!site.leaf)
    return;
  const int sibling_first = site.sibling ? first_leaf_index(site.sibling) : -1;

  if (!remove_leaf(m_root, paneIndex))
    return;

  m_panes.remove(paneIndex);
  renumber_leaves(m_root.get(), paneIndex);
  if (m_focused_pane == paneIndex) {
    if (sibling_first >= 0)
      m_focused_pane = sibling_first > paneIndex ? sibling_first - 1 : sibling_first;
    else
      m_focused_pane = m_panes.isEmpty() ? -1 : 0;
  } else if (m_focused_pane > paneIndex) {
    --m_focused_pane;
  }
}

QVector<int> PaneTree::panesInReadingOrder() const
{
  QVector<int> order;
  collect_reading_order(m_root.get(), order);
  return order;
}

QVector<QRect> PaneTree::layout(const QSize &area) const
{
  QVector<QRect> rects(m_panes.size());
  layout_node(m_root.get(), QRect(QPoint(0, 0), area), rects);
  return rects;
}
