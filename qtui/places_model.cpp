#include "places_model.h"

#include <QDir>
#include <QFileInfo>
#include <QModelIndex>
#include <QStandardPaths>

PlacesModel::PlacesModel(QObject *parent)
  : QAbstractListModel(parent)
{
  // PR 3 subset: only local places that exist; devices and remote locations
  // arrive with the portal work.
  const auto addExisting = [this](const QString &label, QStandardPaths::StandardLocation location) {
    const QStringList locations = QStandardPaths::standardLocations(location);
    if (!locations.isEmpty() && QFileInfo::exists(locations.first()))
      m_places.append({label, locations.first()});
  };

  addExisting(QStringLiteral("Home"), QStandardPaths::HomeLocation);
  addExisting(QStringLiteral("Desktop"), QStandardPaths::DesktopLocation);
  addExisting(QStringLiteral("Documents"), QStandardPaths::DocumentsLocation);
  addExisting(QStringLiteral("Downloads"), QStandardPaths::DownloadLocation);
  addExisting(QStringLiteral("Pictures"), QStandardPaths::PicturesLocation);
  addExisting(QStringLiteral("Music"), QStandardPaths::MusicLocation);
  addExisting(QStringLiteral("Videos"), QStandardPaths::MoviesLocation);

  const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  if (!dataHome.isEmpty()) {
    const QString trash = QDir(dataHome).filePath(QStringLiteral("Trash/files"));
    if (QFileInfo::exists(trash))
      m_places.append({QStringLiteral("Trash"), trash});
  }
  m_places.append({QStringLiteral("File System"), QStringLiteral("/")});
}

int PlacesModel::rowCount(const QModelIndex &parent) const
{
  return parent.isValid() ? 0 : m_places.size();
}

QVariant PlacesModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid() || index.row() < 0 || index.row() >= m_places.size())
    return {};

  const Place &place = m_places.at(index.row());
  switch (role) {
  case LabelRole: return place.label;
  case PathRole: return place.path;
  default: return {};
  }
}

QHash<int, QByteArray> PlacesModel::roleNames() const
{
  return {{LabelRole, "label"}, {PathRole, "path"}};
}
