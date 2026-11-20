#include "panel_tree.h"

#include <QtGlobal>

namespace {

// Leaves are visited left to right, top to bottom: the same order the legacy
// panels_ordered() walks the tree.
void collect_reading_order(const PanelNode *node, QVector<int> &out)
{
  if (!node)
    return;
  if (node->kind == PanelNode::PanelLeaf) {
    out.append(node->panelIndex);
    return;
  }
  collect_reading_order(node->a.get(), out);
  collect_reading_order(node->b.get(), out);
}

PanelNode *find_leaf(PanelNode *node, int panel_index)
{
  if (!node)
    return nullptr;
  if (node->kind == PanelNode::PanelLeaf)
    return node->panelIndex == panel_index ? node : nullptr;
  if (PanelNode *found = find_leaf(node->a.get(), panel_index))
    return found;
  return find_leaf(node->b.get(), panel_index);
}

// panel_remove() activates node_first_panel(sib) when the active panel goes.
struct LeafSite {
  PanelNode *leaf = nullptr;
  PanelNode *sibling = nullptr;  // null when the leaf is the whole tree
};

LeafSite find_leaf_site(PanelNode *node, int panel_index)
{
  if (!node)
    return {};
  if (node->kind == PanelNode::PanelLeaf)
    return node->panelIndex == panel_index ? LeafSite{node, nullptr} : LeafSite{};
  if (node->a && node->a->kind == PanelNode::PanelLeaf
      && node->a->panelIndex == panel_index)
    return LeafSite{node->a.get(), node->b.get()};
  if (node->b && node->b->kind == PanelNode::PanelLeaf
      && node->b->panelIndex == panel_index)
    return LeafSite{node->b.get(), node->a.get()};
  LeafSite found = find_leaf_site(node->a.get(), panel_index);
  if (found.leaf)
    return found;
  return find_leaf_site(node->b.get(), panel_index);
}

int first_leaf_index(PanelNode *node)
{
  while (node && node->kind != PanelNode::PanelLeaf)
    node = node->a.get();
  return node ? node->panelIndex : -1;
}

// Removing a leaf collapses its direct parent into the sibling; ancestors keep
// their splits because the recursion stops there. A root leaf leaves no tree.
bool remove_leaf(std::unique_ptr<PanelNode> &slot, int panel_index)
{
  if (!slot)
    return false;
  if (slot->kind == PanelNode::PanelLeaf) {
    if (slot->panelIndex != panel_index)
      return false;
    slot.reset();
    return true;
  }
  if (slot->a && slot->a->kind == PanelNode::PanelLeaf
      && slot->a->panelIndex == panel_index) {
    slot = std::move(slot->b);
    return true;
  }
  if (slot->b && slot->b->kind == PanelNode::PanelLeaf
      && slot->b->panelIndex == panel_index) {
    slot = std::move(slot->a);
    return true;
  }
  return remove_leaf(slot->a, panel_index) || remove_leaf(slot->b, panel_index);
}

// A panel's index is its position in panels(), so the leaves after a closed
// panel have to shift down with it.
void renumber_leaves(PanelNode *node, int removed_index)
{
  if (!node)
    return;
  if (node->kind == PanelNode::PanelLeaf) {
    if (node->panelIndex > removed_index)
      --node->panelIndex;
    return;
  }
  renumber_leaves(node->a.get(), removed_index);
  renumber_leaves(node->b.get(), removed_index);
}

// The divider sits in the single pixel between the children, so the second
// child loses one pixel of extent in the split direction.
void layout_node(const PanelNode *node, const QRect &rect, QVector<QRect> &out)
{
  if (!node)
    return;
  if (node->kind == PanelNode::PanelLeaf) {
    if (node->panelIndex >= 0 && node->panelIndex < out.size())
      out[node->panelIndex] = rect;
    return;
  }
  if (node->kind == PanelNode::HSplit) {
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

PanelTree::PanelTree()
{
  Panel first;
  first.tabs.append(PanelTab{});
  m_panels.append(first);

  auto leaf = std::make_unique<PanelNode>();
  leaf->kind = PanelNode::PanelLeaf;
  leaf->panelIndex = 0;
  m_root = std::move(leaf);
}

QVector<Panel> &PanelTree::panels()
{
  return m_panels;
}

const QVector<Panel> &PanelTree::panels() const
{
  return m_panels;
}

PanelNode *PanelTree::root() const
{
  return m_root.get();
}

int PanelTree::activePanel() const
{
  return m_active_panel;
}

void PanelTree::setActivePanel(int index)
{
  if (index >= 0 && index < m_panels.size())
    m_active_panel = index;
}

int PanelTree::splitPanel(int panelIndex, Zone zone, int tabIndex, const QString &path)
{
  if (panelIndex < 0 || panelIndex >= m_panels.size() || m_panels.size() >= MaxPanels)
    return -1;
  // Resolve the leaf with the guards: a refused split must not touch any tab.
  PanelNode *leaf = find_leaf(m_root.get(), panelIndex);
  if (!leaf)
    return -1;
  // Moving the only tab would leave the panel empty; the legacy closes a panel
  // when its last tab goes, so the tree refuses that split here.
  if (tabIndex >= 0 && (tabIndex >= m_panels[panelIndex].tabs.size()
                        || m_panels[panelIndex].tabs.size() < 2))
    return -1;

  Panel panel;
  if (tabIndex >= 0) {
    PanelTab moved = m_panels[panelIndex].tabs.takeAt(tabIndex);
    panel.tabs.append(moved);
    // The cursor follows its tab. If that tab was current, the legacy's
    // panel_neighbor() picks the next tab in the panel, or the last one when
    // there is no next.
    if (m_panels[panelIndex].currentTab > tabIndex) {
      --m_panels[panelIndex].currentTab;
    } else if (m_panels[panelIndex].currentTab >= m_panels[panelIndex].tabs.size()) {
      m_panels[panelIndex].currentTab = m_panels[panelIndex].tabs.size() - 1;
    }
  } else {
    PanelTab tab;
    tab.path = path;
    panel.tabs.append(tab);
  }

  const int new_index = m_panels.size();
  m_panels.append(panel);

  auto old_leaf = std::make_unique<PanelNode>();
  old_leaf->kind = PanelNode::PanelLeaf;
  old_leaf->panelIndex = panelIndex;

  auto new_leaf = std::make_unique<PanelNode>();
  new_leaf->kind = PanelNode::PanelLeaf;
  new_leaf->panelIndex = new_index;

  leaf->kind = zone == Zone::Left || zone == Zone::Right
      ? PanelNode::HSplit : PanelNode::VSplit;
  leaf->panelIndex = -1;
  leaf->ratio = 0.5;
  const bool first = zone == Zone::Left || zone == Zone::Top;
  leaf->a = first ? std::move(new_leaf) : std::move(old_leaf);
  leaf->b = first ? std::move(old_leaf) : std::move(new_leaf);
  // panel_split() activates the panel it just made.
  m_active_panel = new_index;
  return new_index;
}

void PanelTree::closePanel(int panelIndex)
{
  if (panelIndex < 0 || panelIndex >= m_panels.size())
    return;

  // Capture the sibling's first panel before the tree gives it the closed
  // leaf's place; its index shifts down with the panel list afterwards.
  const LeafSite site = find_leaf_site(m_root.get(), panelIndex);
  if (!site.leaf)
    return;
  const int sibling_first = site.sibling ? first_leaf_index(site.sibling) : -1;

  if (!remove_leaf(m_root, panelIndex))
    return;

  m_panels.remove(panelIndex);
  renumber_leaves(m_root.get(), panelIndex);
  if (m_active_panel == panelIndex) {
    if (sibling_first >= 0)
      m_active_panel = sibling_first > panelIndex ? sibling_first - 1 : sibling_first;
    else
      m_active_panel = m_panels.isEmpty() ? -1 : 0;
  } else if (m_active_panel > panelIndex) {
    --m_active_panel;
  }
}

QVector<int> PanelTree::panelsInReadingOrder() const
{
  QVector<int> order;
  collect_reading_order(m_root.get(), order);
  return order;
}

QVector<QRect> PanelTree::layout(const QSize &area) const
{
  QVector<QRect> rects(m_panels.size());
  layout_node(m_root.get(), QRect(QPoint(0, 0), area), rects);
  return rects;
}
