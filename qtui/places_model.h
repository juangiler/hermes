#pragma once
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

class PlacesModel : public QAbstractListModel
{
  Q_OBJECT

public:
  enum Role { LabelRole = Qt::UserRole + 1, PathRole };
  Q_ENUM(Role)

  explicit PlacesModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

private:
  struct Place {
    QString label;
    QString path;
  };

  QVector<Place> m_places;
};
