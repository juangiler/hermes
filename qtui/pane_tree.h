#pragma once
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

class DirectoryModel;
class DirectoryWatcher;

// One pane: one directory view and the state that belongs to it. The model and
// the watcher belong to the pane but are owned by the controller.
struct PaneState {
  QString path;
  QStringList history;
  int historyIndex = -1;
  QStringList selectedPaths;  // the selection, as the paths the view shows
  int currentIndex = 0;       // the cursor row
  int anchorIndex = 0;        // the selection anchor
};

struct Pane {
  PaneState state;
  DirectoryModel *model = nullptr;
  DirectoryWatcher *watcher = nullptr;
};

// A node of a tab's split tree. A leaf holds a pane; a split holds two children
// and the ratio between them.
struct PaneNode {
  enum Kind { PaneLeaf, HSplit, VSplit };
  Kind kind = PaneLeaf;
  double ratio = 0.5;
  int paneIndex = -1;               // a leaf's pane
  std::unique_ptr<PaneNode> a, b;   // a split's children
};

// The pane tree of one tab and the operations on it.
class PaneTree {
 public:
  static constexpr int MaxPanes = 16;

  // Which side of the split the new pane takes. Left and Top put it first.
  enum class Zone { Left, Right, Top, Bottom };

  PaneTree();  // one pane with one empty path
  QVector<Pane> &panes();
  const QVector<Pane> &panes() const;
  PaneNode *root() const;
  int focusedPane() const;
  void setFocusedPane(int index);

  // Splits the pane and returns the new pane's index, or -1 at MaxPanes.
  // The new pane shows path and becomes the focused pane.
  int splitPane(int paneIndex, Zone zone, const QString &path);

  // Removes the pane and its state. The sibling of its leaf takes the leaf's
  // place, so the space it leaves goes to that sibling. If the removed pane
  // was focused, the first pane in reading order becomes focused.
  void closePane(int paneIndex);

  // The panes in reading order: left to right, top to bottom.
  QVector<int> panesInReadingOrder() const;

  // The rectangle of every pane, in the order of panes().
  QVector<QRect> layout(const QSize &area) const;

 private:
  QVector<Pane> m_panes;
  std::unique_ptr<PaneNode> m_root;
  int m_focused_pane = 0;
};
