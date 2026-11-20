#pragma once
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

class DirectoryModel;
class DirectoryWatcher;

// One tab on a panel's strip: the directory it shows and the state that
// belongs to that tab rather than to the panel.
struct PanelTab {
  QString path;
  QStringList history;
  int historyIndex = -1;
  QStringList selectedPaths;  // the selection, as the paths the view shows
  int currentIndex = 0;       // the cursor row
  int anchorIndex = 0;        // the selection anchor
};

// A panel: a strip of tabs and the tab that is showing. The model and the
// watcher belong to the panel but are owned by the controller, which creates
// them with the panel and deletes them when it closes.
struct Panel {
  QVector<PanelTab> tabs;
  int currentTab = 0;
  DirectoryModel *model = nullptr;
  DirectoryWatcher *watcher = nullptr;
};

// A node of the split tree. A leaf holds a panel; a split holds two children
// and the ratio between them.
struct PanelNode {
  enum Kind { PanelLeaf, HSplit, VSplit };
  Kind kind = PanelLeaf;
  double ratio = 0.5;
  int panelIndex = -1;               // a leaf's panel
  std::unique_ptr<PanelNode> a, b;   // a split's children
};

// The tree of panels and the operations on it.
class PanelTree {
public:
  static constexpr int MaxPanels = 16;

  // Which side of the split the new panel takes. Left and Top put it first.
  enum class Zone { Left, Right, Top, Bottom };

  PanelTree();  // one panel with one empty tab
  QVector<Panel> &panels();
  const QVector<Panel> &panels() const;
  PanelNode *root() const;
  int activePanel() const;
  void setActivePanel(int index);

  // Splits the panel and returns the new panel's index, or -1 when the limit
  // of MaxPanels is reached. When tabIndex is not negative that tab moves
  // into the new panel; otherwise the new panel opens path. The new panel is
  // placed on zone's side of the split.
  int splitPanel(int panelIndex, Zone zone, int tabIndex, const QString &path);

  // Removes the panel and its tabs. The sibling of its leaf takes the leaf's
  // place, so the space it leaves goes to that sibling.
  void closePanel(int panelIndex);

  // The panels in reading order: left to right, top to bottom.
  QVector<int> panelsInReadingOrder() const;

  // The rectangle of every panel, in the order of panels().
  QVector<QRect> layout(const QSize &area) const;

private:
  QVector<Panel> m_panels;
  std::unique_ptr<PanelNode> m_root;
  int m_active_panel = 0;
};
