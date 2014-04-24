#include "QuickModsList.h"

#include <QMimeData>
#include <QIcon>
#include <QDebug>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "logic/net/CacheDownload.h"
#include "logic/net/NetJob.h"
#include "QuickModFilesUpdater.h"
#include "QuickMod.h"
#include "QuickModVersion.h"
#include "logic/Mod.h"
#include "logic/BaseInstance.h"
#include "logic/OneSixInstance.h"
#include "depends/settings/setting.h"
#include "MultiMC.h"
#include "logic/lists/InstanceList.h"
#include "modutils.h"

#include "depends/settings/inisettingsobject.h"

// TODO updating of mods

QuickModsList::QuickModsList(const Flags flags, QObject *parent)
	: QAbstractListModel(parent), m_updater(new QuickModFilesUpdater(this)),
	  m_settings(new INISettingsObject(QDir::current().absoluteFilePath("quickmod.cfg"), this))
{
	m_settings->registerSetting("AvailableMods",
								QVariant::fromValue(QMap<QString, QMap<QString, QString>>()));
	m_settings->registerSetting("TrustedWebsites", QVariantList());

	connect(m_updater, &QuickModFilesUpdater::error, this, &QuickModsList::error);

	if (!flags.testFlag(DontCleanup))
	{
		cleanup();
	}
}

QuickModsList::~QuickModsList()
{
	delete m_settings;
}


int QuickModsList::getQMIndex(QuickMod *mod) const
{
	for (int i = 0; i < m_mods.count(); i++)
	{
		if (mod == m_mods[i].get())
		{
			return i;
		}
	}
	return -1;
}

QHash<int, QByteArray> QuickModsList::roleNames() const
{
	QHash<int, QByteArray> roles;
	roles[NameRole] = "name";
	roles[WebsiteRole] = "website";
	roles[IconRole] = "icon";
	roles[LogoRole] = "logo";
	roles[UpdateRole] = "update";
	roles[ReferencesRole] = "recommendedUrls";
	roles[DependentUrlsRole] = "dependentUrls";
	roles[NemNameRole] = "nemName";
	roles[ModIdRole] = "modId";
	roles[CategoriesRole] = "categories";
	roles[TagsRole] = "tags";
	return roles;
}

int QuickModsList::rowCount(const QModelIndex &) const
{
	return m_mods.size(); // <-----
}
Qt::ItemFlags QuickModsList::flags(const QModelIndex &index) const
{
	return Qt::ItemIsDropEnabled | Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant QuickModsList::data(const QModelIndex &index, int role) const
{
	if (0 > index.row() || index.row() >= m_mods.size())
	{
		return QVariant();
	}

	QuickModPtr mod = m_mods.at(index.row());

	switch (role)
	{
	case Qt::DisplayRole:
		return mod->name();
	case Qt::DecorationRole:
		if (mod->icon().isNull())
		{
			return QColor(0, 0, 0, 0);
		}
		else
		{
			return mod->icon();
		}
	case Qt::ToolTipRole:
		return mod->description();
	case NameRole:
		return mod->name();
	case UidRole:
		return mod->uid();
	case DescriptionRole:
		return mod->description();
	case WebsiteRole:
		return mod->websiteUrl();
	case IconRole:
		return mod->icon();
	case LogoRole:
		return mod->logo();
	case UpdateRole:
		return mod->updateUrl();
	case ReferencesRole:
		return QVariant::fromValue(mod->references());
	case NemNameRole:
		return mod->nemName();
	case ModIdRole:
		return mod->modId();
	case CategoriesRole:
		return mod->categories();
	case TagsRole:
		return mod->tags();
	case QuickModRole:
		return QVariant::fromValue(mod);
	case IsStubRole:
		return mod->isStub();
	}

	return QVariant();
}

bool QuickModsList::canDropMimeData(const QMimeData *data, Qt::DropAction action, int row,
									int column, const QModelIndex &parent) const
{
	if (action != Qt::CopyAction)
	{
		return false;
	}
	return data->hasText() | data->hasUrls();
}
bool QuickModsList::dropMimeData(const QMimeData *data, Qt::DropAction action, int row,
								 int column, const QModelIndex &parent)
{
	if (!canDropMimeData(data, action, row, column, parent))
	{
		return false;
	}

	if (data->hasText())
	{
		registerMod(data->text().toUtf8());
	}
	else if (data->hasUrls())
	{
		foreach(const QUrl & url, data->urls())
		{
			registerMod(url);
		}
	}
	else
	{
		return false;
	}

	return true;
}
Qt::DropActions QuickModsList::supportedDropActions() const
{
	return Qt::CopyAction;
}
Qt::DropActions QuickModsList::supportedDragActions() const
{
	return 0;
}

QuickModPtr QuickModsList::modForModId(const QString &modId) const
{
	if (modId.isEmpty())
	{
		return 0;
	}
	foreach (QuickModPtr mod, m_mods)
	{
		if (mod->modId() == modId)
		{
			return mod;
		}
	}

	return 0;
}
QuickModPtr QuickModsList::mod(const QString &uid) const
{
	foreach (QuickModPtr mod, m_mods)
	{
		if (mod->uid() == uid)
		{
			return mod;
		}
	}

	return 0;
}
QuickModVersionPtr QuickModsList::modVersion(const QString &modUid, const QString &versionName) const
{
	QuickModPtr m = mod(modUid);
	if (m)
	{
		return m->version(versionName);
	}
	else
	{
		return 0;
	}
}

void QuickModsList::modAddedBy(const Mod &mod, BaseInstance *instance)
{
	QuickModPtr qMod = m_updater->ensureExists(mod);
	markModAsInstalled(qMod, QuickModVersion::invalid(qMod), mod.filename().absoluteFilePath(),
					   instance);
}
void QuickModsList::modRemovedBy(const Mod &mod, BaseInstance *instance)
{
	QuickModPtr qMod = m_updater->ensureExists(mod);
	markModAsUninstalled(qMod, QuickModVersion::invalid(qMod), instance);
}

void QuickModsList::markModAsExists(QuickModPtr mod, const BaseVersionPtr version,
									const QString &fileName)
{
	auto mods = m_settings->get("AvailableMods").toMap();
	auto map = mods[mod->uid()].toMap();
	map[version->name()] = fileName;
	mods[mod->uid()] = map;
	m_settings->getSetting("AvailableMods")->set(QVariant(mods));
}

void QuickModsList::markModAsInstalled(QuickModPtr mod, const BaseVersionPtr version,
									   const QString &fileName, BaseInstance *instance)
{
	auto mods = instance->settings().get("InstalledMods").toMap();
	auto map = mods[mod->uid()].toMap();
	map[version->name()] = fileName;
	mods[mod->uid()] = map;
	instance->settings().getSetting("InstalledMods")->set(QVariant(mods));
}
void QuickModsList::markModAsUninstalled(QuickModPtr mod, const BaseVersionPtr version,
										 BaseInstance *instance)
{
	auto mods = instance->settings().get("InstalledMods").toMap();
	auto map = mods[mod->uid()].toMap();
	map.remove(version->name());
	if (map.isEmpty())
	{
		mods.remove(mod->uid());
	}
	else
	{
		mods[mod->uid()] = map;
	}
	instance->settings().set("InstalledMods", QVariant(mods));
}
bool QuickModsList::isModMarkedAsInstalled(QuickModPtr mod, const BaseVersionPtr version,
										   BaseInstance *instance) const
{
	if (!isModMarkedAsExists(mod, version))
	{
		return false;
	}
	auto mods = instance->settings().get("InstalledMods").toMap();
	return mods.contains(mod->uid()) &&
		   mods.value(mod->uid()).toMap().contains(version->name());
}
bool QuickModsList::isModMarkedAsExists(QuickModPtr mod, const BaseVersionPtr version) const
{
	return isModMarkedAsExists(mod, version->name());
}
bool QuickModsList::isModMarkedAsExists(QuickModPtr mod, const QString &version) const
{
	auto mods = m_settings->get("AvailableMods").toMap();
	return mods.contains(mod->uid()) &&
			mods.value(mod->uid()).toMap().contains(version);
}
QMap<QString, QString> QuickModsList::installedModFiles(QuickModPtr mod, BaseInstance *instance) const
{
	auto mods = instance->settings().get("InstalledMods").toMap();
	auto tmp = mods[mod->uid()].toMap();
	QMap<QString, QString> out;
	for (auto it = tmp.begin(); it != tmp.end(); ++it)
	{
		out.insert(it.key(), it.value().toString());
	}
	return out;
}
QString QuickModsList::existingModFile(QuickModPtr mod, const BaseVersionPtr version) const
{
	return existingModFile(mod, version->name());
}
QString QuickModsList::existingModFile(QuickModPtr mod, const QString &version) const
{
	if (!isModMarkedAsExists(mod, version))
	{
		return QString();
	}
	auto mods = m_settings->get("AvailableMods").toMap();
	return mods[mod->uid()].toMap()[version].toString();
}

bool QuickModsList::isWebsiteTrusted(const QUrl &url) const
{
	auto websites = m_settings->get("TrustedWebsites").toList();
	return websites.contains(url.toString());
}
void QuickModsList::setWebsiteTrusted(const QUrl &url, const bool trusted)
{
	auto websites = m_settings->get("TrustedWebsites").toList();
	if (trusted && !websites.contains(url.toString()))
	{
		websites.append(url.toString());
	}
	else if (!trusted)
	{
		websites.removeAll(url.toString());
	}
	m_settings->getSetting("TrustedWebsites")->set(websites);
}

bool QuickModsList::haveUid(const QString &uid) const
{
	for (auto mod : m_mods)
	{
		if (mod->uid() == uid)
		{
			return true;
		}
	}
	return false;
}

QList<QuickModPtr> QuickModsList::updatedModsForInstance(std::shared_ptr<BaseInstance> instance) const
{
	QList<QuickModPtr> mods;
	std::shared_ptr<OneSixInstance> onesix = std::dynamic_pointer_cast<OneSixInstance>(instance);
	for (auto it = onesix->getFullVersion()->quickmods.begin(); it != onesix->getFullVersion()->quickmods.end(); ++it)
	{
		if (it.value().isEmpty())
		{
			continue;
		}
		QuickModPtr m = mod(it.key());
		if (!m)
		{
			continue;
		}
		if (!m->latestVersion(instance->intendedVersionId()))
		{
			continue;
		}
		if (Util::Version(it.value()) < Util::Version(m->latestVersion(instance->intendedVersionId())->name()))
		{
			mods.append(m);
		}
	}
	return mods;
}

void QuickModsList::registerMod(const QString &fileName)
{
	registerMod(QUrl::fromLocalFile(fileName));
}
void QuickModsList::registerMod(const QUrl &url)
{
	m_updater->registerFile(url);
}

void QuickModsList::updateFiles()
{
	m_updater->update();
}

void QuickModsList::touchMod(QuickModPtr mod)
{
	emit modAdded(mod);
}

void QuickModsList::addMod(QuickModPtr mod)
{
	connect(mod.get(), &QuickMod::iconUpdated, this, &QuickModsList::modIconUpdated);
	connect(mod.get(), &QuickMod::logoUpdated, this, &QuickModsList::modLogoUpdated);

	for (int i = 0; i < m_mods.size(); ++i)
	{
		if (m_mods.at(i)->compare(mod))
		{
			disconnect(m_mods.at(i).get(), &QuickMod::iconUpdated, this,
					   &QuickModsList::modIconUpdated);
			disconnect(m_mods.at(i).get(), &QuickMod::logoUpdated, this,
					   &QuickModsList::modLogoUpdated);
			m_mods.replace(i, mod);

			emit modAdded(mod);
			emit modsListChanged();

			return;
		}
	}

	beginInsertRows(QModelIndex(), 0, 0);
	m_mods.prepend(mod);
	endInsertRows();

	emit modAdded(mod);
	emit modsListChanged();
}
void QuickModsList::clearMods()
{
	beginResetModel();
	m_mods.clear();
	endResetModel();

	emit modsListChanged();
}
void QuickModsList::removeMod(QuickModPtr mod)
{
	if (!m_mods.contains(mod))
	{
		return;
	}

	disconnect(mod.get(), &QuickMod::iconUpdated, this, &QuickModsList::modIconUpdated);
	disconnect(mod.get(), &QuickMod::logoUpdated, this, &QuickModsList::modLogoUpdated);

	beginRemoveRows(QModelIndex(), m_mods.indexOf(mod), m_mods.indexOf(mod));
	m_mods.takeAt(m_mods.indexOf(mod));
	endRemoveRows();

	emit modsListChanged();
}

void QuickModsList::modIconUpdated()
{
	auto mod = qobject_cast<QuickMod*>(sender());
	auto modIndex = index(getQMIndex(mod), 0);
	emit dataChanged(modIndex, modIndex, QVector<int>() << Qt::DecorationRole << IconRole);
}
void QuickModsList::modLogoUpdated()
{
	auto mod = qobject_cast<QuickMod*>(sender());
	auto modIndex = index(getQMIndex(mod), 0);
	emit dataChanged(modIndex, modIndex, QVector<int>() << LogoRole);
}

void QuickModsList::cleanup()
{
	// ensure that only mods that really exist on the local filesystem are marked as available
	QDir dir;
	auto mods = m_settings->get("AvailableMods").toMap();
	QMutableMapIterator<QString, QVariant> modIt(mods);
	while (modIt.hasNext())
	{
		auto versions = modIt.next().value().toMap();
		QMutableMapIterator<QString, QVariant> versionIt(versions);
		while (versionIt.hasNext())
		{
			if (!dir.exists(versionIt.next().value().toString()))
			{
				versionIt.remove();
			}
		}
		modIt.setValue(versions);
		if (modIt.value().toMap().isEmpty())
		{
			modIt.remove();
		}
	}
	m_settings->set("AvailableMods", mods);
}

void QuickModsList::unregisterMod(QuickModPtr mod)
{
	int row = m_mods.indexOf(mod);
	if (row == -1)
	{
		return;
	}
	beginRemoveRows(QModelIndex(), row, row);
	m_mods.removeAt(row);
	endRemoveRows();
	m_updater->unregisterMod(mod);
}