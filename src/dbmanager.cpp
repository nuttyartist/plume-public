#include "dbmanager.h"
#include <QtSql/QSqlQuery>
#include <QTimeZone>
#include <QDateTime>
#include <QDebug>
#include <QSqlError>
#include <QtConcurrent>
#include <QSqlRecord>
#include <QSet>

#define DEFAULT_DATABASE_NAME "default_database"
#define OUTSIDE_DATABASE_NAME "outside_database"

/*!
 * \brief DBManager::DBManager
 * \param parent
 */
DBManager::DBManager(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<QList<NodeData *>>("QList<NodeData*>");
    qRegisterMetaType<QVector<NodeData>>("QVector<NodeData>");
    qRegisterMetaType<NodeTagTreeData>("NodeTagTreeData");
    qRegisterMetaType<QSet<int>>("QSet<int>");
    qRegisterMetaType<ListViewInfo>("ListViewInfo");
    qRegisterMetaType<FolderListType>("DBManager::FolderListType");
}

/*!
 * \brief DBManager::open
 * \param path
 * \param doCreate
 */
void DBManager::open(const QString &path, bool doCreate)
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", DEFAULT_DATABASE_NAME);
    m_dbpath = path;
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        qDebug() << "Error: connection with database fail";
    } else {
        qDebug() << "Database: connection ok";
    }

    if (doCreate) {
        createTables();
    }
    recalculateChildNotesCount();
}

/*!
 * \brief DBManager::createTables
 */
void DBManager::createTables()
{
    m_db.transaction();
    QSqlQuery query(m_db);

    QString nodeTable = R"(CREATE TABLE "node_table" ()"
                        R"(    "id"	INTEGER NOT NULL,)"
                        R"(    "title"	TEXT,)"
                        R"(    "creation_date"	INTEGER NOT NULL DEFAULT 0,)"
                        R"(    "modification_date"	INTEGER NOT NULL DEFAULT 0,)"
                        R"(    "deletion_date"	INTEGER NOT NULL DEFAULT 0,)"
                        R"(    "content"	TEXT,)"
                        R"(    "node_type"	INTEGER NOT NULL,)"
                        R"(    "parent_id"	INTEGER NOT NULL,)"
                        R"(    "relative_position"	INTEGER NOT NULL,)"
                        R"(    "scrollbar_position"	INTEGER NOT NULL,)"
                        R"(    "absolute_path"	TEXT NOT NULL,)"
                        R"(    "is_pinned_note"	INTEGER NOT NULL DEFAULT 0,)"
                        R"(    "relative_position_an"	INTEGER NOT NULL,)"
                        R"(    "child_notes_count"	INTEGER NOT NULL)"
                        R"();)";
    auto status = query.exec(nodeTable);
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.clear();
    QString tagRelationship = R"(CREATE TABLE "tag_relationship" ()"
                              R"(    "node_id"	INTEGER NOT NULL,)"
                              R"(    "tag_id"	INTEGER NOT NULL,)"
                              R"(    UNIQUE(node_id, tag_id))"
                              R"();)";
    status = query.exec(tagRelationship);
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.clear();

    QString tagTable = R"(CREATE TABLE "tag_table" ()"
                       R"(    "id"	INTEGER NOT NULL,)"
                       R"(    "name"	TEXT NOT NULL,)"
                       R"(    "color"	TEXT NOT NULL,)"
                       R"(    "child_notes_count"	INTEGER NOT NULL,)"
                       R"(    "relative_position"	INTEGER NOT NULL)"
                       R"();)";
    status = query.exec(tagTable);
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.clear();

    QString metadata = R"(CREATE TABLE "metadata" ()"
                       R"(    "key"	TEXT NOT NULL,)"
                       R"(    "value"	INTEGER NOT NULL)"
                       R"();)";
    status = query.exec(metadata);
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.clear();

    query.prepare(R"(INSERT INTO "metadata"("key","value") VALUES (:key, :value);)");
    query.bindValue(":key", "next_node_id");
    query.bindValue(":value", 0);
    status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.bindValue(":key", "next_tag_id");
    query.bindValue(":value", 0);
    status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.clear();

    NodeData rootFolder; // id 0
    rootFolder.setNodeType(NodeData::Folder);
    QDateTime noteDate = QDateTime::currentDateTime();
    rootFolder.setCreationDateTime(noteDate);
    rootFolder.setLastModificationDateTime(noteDate);
    rootFolder.setFullTitle(QStringLiteral("/"));
    rootFolder.setParentId(-1);
    addNode(rootFolder);

    NodeData trashFolder; // id 1
    trashFolder.setNodeType(NodeData::Folder);
    trashFolder.setCreationDateTime(noteDate);
    trashFolder.setLastModificationDateTime(noteDate);
    trashFolder.setFullTitle(QStringLiteral("Trash"));
    trashFolder.setParentId(0);
    addNode(trashFolder);

    NodeData notesFolder; // id 2
    notesFolder.setNodeType(NodeData::Folder);
    notesFolder.setCreationDateTime(noteDate);
    notesFolder.setLastModificationDateTime(noteDate);
    notesFolder.setFullTitle(QStringLiteral("Notes"));
    notesFolder.setParentId(0);
    addNode(notesFolder);

    m_db.commit();
}

/*!
 * \brief DBManager::isNoteExist
 * \param note
 * \return
 */
bool DBManager::isNodeExist(const NodeData &node)
{
    QSqlQuery query(m_db);

    int id = node.id();
    QString queryStr =
            QStringLiteral("SELECT EXISTS(SELECT 1 FROM node_table WHERE id = :id LIMIT 1 )");
    query.prepare(queryStr);
    query.bindValue(":id", id);
    bool status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError() << query.isValid();
    }
    query.next();
    return query.value(0).toInt() == 1;
}

QVector<NodeData> DBManager::getAllFolders()
{
    QVector<NodeData> nodeList;

    QSqlQuery query(m_db);
    query.prepare(R"(SELECT)"
                  R"("id",)"
                  R"("title",)"
                  R"("creation_date",)"
                  R"("modification_date",)"
                  R"("deletion_date",)"
                  R"("content",)"
                  R"("node_type",)"
                  R"("parent_id",)"
                  R"("relative_position",)"
                  R"("absolute_path", )"
                  R"("child_notes_count" )"
                  R"(FROM node_table WHERE node_type=:node_type;)");
    query.bindValue(":node_type", static_cast<int>(NodeData::Type::Folder));
    bool status = query.exec();
    if (status) {
        while (query.next()) {
            NodeData node;
            node.setId(query.value(0).toInt());
            node.setFullTitle(query.value(1).toString());
            node.setCreationDateTime(QDateTime::fromMSecsSinceEpoch(query.value(2).toLongLong()));
            node.setLastModificationDateTime(
                    QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong()));
            node.setDeletionDateTime(QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong()));
            node.setContent(query.value(5).toString());
            node.setNodeType(static_cast<NodeData::Type>(query.value(6).toInt()));
            node.setParentId(query.value(7).toInt());
            node.setRelativePosition(query.value(8).toInt());
            node.setAbsolutePath(query.value(9).toString());
            node.setChildNotesCount(query.value(10).toInt());
            if (node.nodeType() == NodeData::Note) {
                node.setTagIds(getAllTagForNote(node.id()));
            }
            nodeList.append(node);
        }
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }

    return nodeList;
}

QVector<TagData> DBManager::getAllTagInfo()
{
    QVector<TagData> tagList;

    QSqlQuery query(m_db);
    query.prepare(
            R"(SELECT "id","name","color","relative_position","child_notes_count" FROM tag_table;)");
    bool status = query.exec();
    if (status) {
        while (query.next()) {
            TagData tag;
            tag.setId(query.value(0).toInt());
            tag.setName(query.value(1).toString());
            tag.setColor(query.value(2).toString());
            tag.setRelativePosition(query.value(3).toInt());
            tag.setChildNotesCount(query.value(4).toInt());
            tagList.append(tag);
        }
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }

    return tagList;
}

QSet<int> DBManager::getAllTagForNote(int noteId)
{
    QSet<int> tagIds;
    QSqlQuery query(m_db);
    query.prepare(R"(SELECT "tag_id" FROM tag_relationship WHERE node_id = :node_id;)");
    query.bindValue(":node_id", noteId);
    bool status = query.exec();
    if (status) {
        while (query.next()) {
            tagIds.insert(query.value(0).toInt());
        }
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    return tagIds;
}

int DBManager::addNode(const NodeData &node)
{
    QSqlQuery query(m_db);
    QString emptyStr;

    qint64 epochTimeDateCreated = node.creationDateTime().toMSecsSinceEpoch();
    QString content = node.content().replace("'", "''").replace(QChar('\x0'), emptyStr);
    QString fullTitle = node.fullTitle().replace("'", "''").replace(QChar('\x0'), emptyStr);

    qint64 epochTimeDateLastModified = node.lastModificationdateTime().isNull()
            ? epochTimeDateCreated
            : node.lastModificationdateTime().toMSecsSinceEpoch();

    int relationalPosition = 0;
    if (node.parentId() != -1) {
        query.prepare(R"(SELECT relative_position FROM "node_table" )"
                      R"(WHERE parent_id = :parent_id AND node_type = :node_type;)");
        query.bindValue(":parent_id", node.parentId());
        query.bindValue(":node_type", static_cast<int>(node.nodeType()));
        bool status = query.exec();
        if (status) {
            while (query.next()) {
                if (relationalPosition <= query.value(0).toInt()) {
                    relationalPosition = query.value(0).toInt() + 1;
                }
            }
        } else {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
        query.finish();
    }
    int nodeId = nextAvailableNodeId();
    QString absolutePath;
    if (node.parentId() != -1) {
        absolutePath = getNodeAbsolutePath(node.parentId()).path();
    }
    absolutePath += PATH_SEPARATOR + QString::number(nodeId);
    QString queryStr =
            R"(INSERT INTO "node_table")"
            R"(("id", "title", "creation_date", "modification_date", "deletion_date", "content", "node_type", "parent_id", "relative_position", "scrollbar_position", "absolute_path", "is_pinned_note", "relative_position_an", "child_notes_count"))"
            R"(VALUES (:id, :title, :creation_date, :modification_date, :deletion_date, :content, :node_type, :parent_id, :relative_position, :scrollbar_position, :absolute_path, :is_pinned_note, :relative_position_an, :child_notes_count);)";

    query.prepare(queryStr);
    query.bindValue(":id", nodeId);
    query.bindValue(":title", fullTitle);
    query.bindValue(":creation_date", epochTimeDateCreated);
    query.bindValue(":modification_date", epochTimeDateLastModified);
    if (node.deletionDateTime().isNull()) {
        query.bindValue(":deletion_date", -1);
    } else {
        query.bindValue(":deletion_date", node.deletionDateTime().toMSecsSinceEpoch());
    }
    query.bindValue(":content", content);
    query.bindValue(":node_type", static_cast<int>(node.nodeType()));
    query.bindValue(":parent_id", node.parentId());
    query.bindValue(":relative_position", relationalPosition);
    query.bindValue(":scrollbar_position", node.scrollBarPosition());
    query.bindValue(":absolute_path", absolutePath);
    query.bindValue(":is_pinned_note", node.isPinnedNote() ? 1 : 0);
    query.bindValue(":relative_position_an", node.relativePosAN());
    query.bindValue(":child_notes_count", node.childNotesCount());

    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.finish();

    query.prepare(R"(UPDATE "metadata" SET "value"=:value WHERE "key"='next_node_id';)");
    query.bindValue(":value", nodeId + 1);
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    if (node.nodeType() == NodeData::Note) {
        increaseChildNotesCountFolder(node.parentId());
        increaseChildNotesCountFolder(SpecialNodeID::RootFolder);
    }
    return nodeId;
}

int DBManager::addNodePreComputed(const NodeData &node)
{
    QSqlQuery query(m_db);
    QString emptyStr;

    qint64 epochTimeDateCreated = node.creationDateTime().toMSecsSinceEpoch();
    QString content = node.content().replace("'", "''").replace(QChar('\x0'), emptyStr);
    QString fullTitle = node.fullTitle().replace("'", "''").replace(QChar('\x0'), emptyStr);

    qint64 epochTimeDateLastModified = node.lastModificationdateTime().isNull()
            ? epochTimeDateCreated
            : node.lastModificationdateTime().toMSecsSinceEpoch();

    int relationalPosition = node.relativePosition();
    int nodeId = node.id();
    QString absolutePath = node.absolutePath();
    QString queryStr =
            R"(INSERT INTO "node_table" )"
            R"(("id", "title", "creation_date", "modification_date", "deletion_date", "content", "node_type", "parent_id", "relative_position", "scrollbar_position", "absolute_path", "is_pinned_note", "relative_position_an", "child_notes_count") )"
            R"(VALUES (:id, :title, :creation_date, :modification_date, :deletion_date, :content, :node_type, :parent_id, :relative_position, :scrollbar_position, :absolute_path, :is_pinned_note, :relative_position_an, :child_notes_count);)";

    query.prepare(queryStr);
    query.bindValue(":id", nodeId);
    query.bindValue(":title", fullTitle);
    query.bindValue(":creation_date", epochTimeDateCreated);
    query.bindValue(":modification_date", epochTimeDateLastModified);
    if (node.deletionDateTime().isNull()) {
        query.bindValue(":deletion_date", -1);
    } else {
        query.bindValue(":deletion_date", node.deletionDateTime().toMSecsSinceEpoch());
    }
    query.bindValue(":content", content);
    query.bindValue(":node_type", static_cast<int>(node.nodeType()));
    query.bindValue(":parent_id", node.parentId());
    query.bindValue(":relative_position", relationalPosition);
    query.bindValue(":scrollbar_position", node.scrollBarPosition());
    query.bindValue(":absolute_path", absolutePath);
    query.bindValue(":is_pinned_note", node.isPinnedNote() ? 1 : 0);
    query.bindValue(":relative_position_an", node.relativePosAN());
    query.bindValue(":child_notes_count", node.childNotesCount());

    bool status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError() << query.isValid();
    }

    query.finish();

    return nodeId;
}

void DBManager::recalculateChildNotesCount()
{
    QSet<int> tagIds;
    QSqlQuery query(m_db);
    query.prepare(R"(SELECT id FROM "tag_table")");
    bool status = query.exec();
    if (status) {
        while (query.next()) {
            tagIds.insert(query.value(0).toInt());
        }
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.clear();
    for (const auto &id : qAsConst(tagIds)) {
        query.prepare("SELECT count(*) FROM tag_relationship WHERE tag_id=:id");
        query.bindValue(QStringLiteral(":id"), id);
        status = query.exec();
        int childNotesCount = 0;
        if (status) {
            query.next();
            childNotesCount = query.value(0).toInt();
        } else {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
        query.clear();
        query.prepare(QStringLiteral("UPDATE tag_table SET child_notes_count = :child_notes_count "
                                     "WHERE id = :id"));
        query.bindValue(QStringLiteral(":id"), id);
        query.bindValue(QStringLiteral(":child_notes_count"), childNotesCount);
        status = query.exec();
        if (!status) {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
        emit childNotesCountUpdatedTag(id, childNotesCount);
    }
    query.clear();
    QMap<int, QString> folderIds;
    query.prepare(R"(SELECT id, absolute_path )"
                  R"(FROM node_table WHERE node_type=:node_type;)");
    query.bindValue(":node_type", static_cast<int>(NodeData::Type::Folder));
    status = query.exec();
    if (status) {
        while (query.next()) {
            auto id = query.value(0).toInt();
            if (id != SpecialNodeID::RootFolder) {
                folderIds[id] = query.value(1).toString();
            }
        }
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    for (const auto &id : folderIds.keys()) {
        query.prepare(R"(SELECT count(*) FROM node_table )"
                      R"(WHERE node_type = (:node_type) AND parent_id = (:parent_id);)");
        query.bindValue(QStringLiteral(":parent_id"), id);
        query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Note));
        status = query.exec();
        int childNotesCount = 0;
        if (status) {
            query.next();
            childNotesCount = query.value(0).toInt();
        } else {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
        query.clear();
        query.prepare(QStringLiteral("UPDATE node_table SET child_notes_count = :child_notes_count "
                                     "WHERE id = :id"));
        query.bindValue(QStringLiteral(":id"), id);
        query.bindValue(QStringLiteral(":child_notes_count"), childNotesCount);
        status = query.exec();
        if (!status) {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
        emit childNotesCountUpdatedFolder(id, folderIds[id], childNotesCount);
    }
    recalculateChildNotesCountAllNotes();
}

void DBManager::recalculateChildNotesCountFolder(int folderId)
{
    QSqlQuery query(m_db);
    query.prepare(R"(SELECT count(*) FROM node_table )"
                  R"(WHERE node_type = (:node_type) AND parent_id = (:parent_id);)");
    query.bindValue(QStringLiteral(":parent_id"), folderId);
    query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Note));
    bool status = query.exec();
    int childNotesCount = 0;
    if (status) {
        query.next();
        childNotesCount = query.value(0).toInt();
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.clear();
    query.prepare(QStringLiteral("UPDATE node_table SET child_notes_count = :child_notes_count "
                                 "WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), folderId);
    query.bindValue(QStringLiteral(":child_notes_count"), childNotesCount);
    status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    emit childNotesCountUpdatedFolder(folderId, getNodeAbsolutePath(folderId).path(),
                                      childNotesCount);
}

void DBManager::recalculateChildNotesCountTag(int tagId)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT count(*) FROM tag_relationship WHERE tag_id=:id");
    query.bindValue(QStringLiteral(":id"), tagId);
    bool status = query.exec();
    int childNotesCount = 0;
    if (status) {
        query.next();
        childNotesCount = query.value(0).toInt();
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.clear();
    query.prepare(QStringLiteral("UPDATE tag_table SET child_notes_count = :child_notes_count "
                                 "WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), tagId);
    query.bindValue(QStringLiteral(":child_notes_count"), childNotesCount);
    status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    emit childNotesCountUpdatedTag(tagId, childNotesCount);
}

void DBManager::recalculateChildNotesCountAllNotes()
{
    QSqlQuery query(m_db);
    query.prepare(R"(SELECT count(*) FROM node_table )"
                  R"(WHERE node_type = (:node_type) AND parent_id != (:parent_id);)");
    query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Note));
    query.bindValue(QStringLiteral(":parent_id"), static_cast<int>(SpecialNodeID::TrashFolder));
    bool status = query.exec();
    int childNotesCount = 0;
    if (status) {
        query.next();
        childNotesCount = query.value(0).toInt();
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.clear();
    query.prepare(QStringLiteral("UPDATE node_table SET child_notes_count = :child_notes_count "
                                 "WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), SpecialNodeID::RootFolder);
    query.bindValue(QStringLiteral(":child_notes_count"), childNotesCount);
    status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    emit childNotesCountUpdatedFolder(SpecialNodeID::RootFolder,
                                      getNodeAbsolutePath(SpecialNodeID::RootFolder).path(),
                                      childNotesCount);
}

void DBManager::increaseChildNotesCountTag(int tagId)
{
    QSqlQuery query(m_db);
    query.prepare(R"(SELECT child_notes_count FROM "tag_table" WHERE id=:id)");
    query.bindValue(QStringLiteral(":id"), tagId);
    bool status = query.exec();
    int childNotesCount = 0;
    if (status) {
        query.next();
        childNotesCount = query.value(0).toInt();
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        return;
    }
    query.clear();
    childNotesCount += 1;

    query.prepare(QStringLiteral("UPDATE tag_table SET child_notes_count = :child_notes_count "
                                 "WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), tagId);
    query.bindValue(QStringLiteral(":child_notes_count"), childNotesCount);
    status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    emit childNotesCountUpdatedTag(tagId, childNotesCount);
}

void DBManager::decreaseChildNotesCountTag(int tagId)
{
    QSqlQuery query(m_db);
    query.prepare(R"(SELECT child_notes_count FROM "tag_table" WHERE id=:id)");
    query.bindValue(QStringLiteral(":id"), tagId);
    bool status = query.exec();
    int childNoteCount = 0;
    if (status) {
        query.next();
        childNoteCount = query.value(0).toInt();
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        return;
    }
    query.clear();
    childNoteCount -= 1;
    if (childNoteCount < 0) {
        childNoteCount = 0;
    }

    query.prepare(QStringLiteral("UPDATE tag_table SET child_notes_count = :child_notes_count "
                                 "WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), tagId);
    query.bindValue(QStringLiteral(":child_notes_count"), childNoteCount);
    status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    emit childNotesCountUpdatedTag(tagId, childNoteCount);
}

void DBManager::increaseChildNotesCountFolder(int folderId)
{
    QSqlQuery query(m_db);
    query.prepare(R"(SELECT child_notes_count, absolute_path  FROM "node_table" WHERE id=:id)");
    query.bindValue(QStringLiteral(":id"), folderId);
    bool status = query.exec();
    int childNotesCount = 0;
    QString absPath;
    if (status) {
        query.next();
        childNotesCount = query.value(0).toInt();
        absPath = query.value(1).toString();
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        return;
    }
    query.clear();
    childNotesCount += 1;

    query.prepare(QStringLiteral("UPDATE node_table SET child_notes_count = :child_notes_count "
                                 "WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), folderId);
    query.bindValue(QStringLiteral(":child_notes_count"), childNotesCount);
    status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    emit childNotesCountUpdatedFolder(folderId, absPath, childNotesCount);
}

void DBManager::decreaseChildNotesCountFolder(int folderId)
{
    QSqlQuery query(m_db);
    query.prepare(R"(SELECT child_notes_count, absolute_path  FROM "node_table" WHERE id=:id)");
    query.bindValue(QStringLiteral(":id"), folderId);
    bool status = query.exec();
    int childNotesCount = 0;
    QString absPath;
    if (status) {
        query.next();
        childNotesCount = query.value(0).toInt();
        absPath = query.value(1).toString();
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        return;
    }
    query.clear();
    childNotesCount -= 1;
    if (childNotesCount < 0) {
        childNotesCount = 0;
    }

    query.prepare(QStringLiteral("UPDATE node_table SET child_notes_count = :child_notes_count "
                                 "WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), folderId);
    query.bindValue(QStringLiteral(":child_notes_count"), childNotesCount);
    status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    emit childNotesCountUpdatedFolder(folderId, absPath, childNotesCount);
}

int DBManager::addTag(const TagData &tag)
{
    QSqlQuery query(m_db);

    int relationalPosition = 0;
    query.prepare(R"(SELECT relative_position FROM "tag_table" )");
    bool status = query.exec();
    if (status) {
        while (query.next()) {
            if (relationalPosition <= query.value(0).toInt()) {
                relationalPosition = query.value(0).toInt() + 1;
            }
        }
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.finish();
    int id = nextAvailableTagId();

    QString queryStr = R"(INSERT INTO "tag_table" )"
                       R"(("id","name","color","relative_position","child_notes_count") )"
                       R"(VALUES (:id, :name, :color, :relative_position, :child_notes_count);)";
    query.prepare(queryStr);
    query.bindValue(":id", id);
    query.bindValue(":name", tag.name());
    query.bindValue(":color", tag.color());
    query.bindValue(":relative_position", relationalPosition);
    query.bindValue(":child_notes_count", tag.childNotesCount());
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.finish();

    query.prepare(R"(UPDATE "metadata" SET "value"=:value WHERE "key"='next_tag_id';)");
    query.bindValue(":value", id + 1);
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    auto newTag = tag;
    newTag.setId(id);
    emit tagAdded(newTag);
    return id;
}

void DBManager::addNoteToTag(int noteId, int tagId)
{
    QSqlQuery query(m_db);
    query.prepare(
            R"(INSERT OR IGNORE INTO "tag_relationship" ("node_id","tag_id") VALUES (:note_id, :tag_id);)");
    query.bindValue(":note_id", noteId);
    query.bindValue(":tag_id", tagId);
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    recalculateChildNotesCountTag(tagId);
}

void DBManager::removeNoteFromTag(int noteId, int tagId)
{
    QSqlQuery query(m_db);
    query.prepare(R"(DELETE FROM "tag_relationship" )"
                  R"(WHERE node_id = (:note_id) AND tag_id = (:tag_id);)");
    query.bindValue(":note_id", noteId);
    query.bindValue(":tag_id", tagId);
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    decreaseChildNotesCountTag(tagId);
}

int DBManager::nextAvailableNodeId()
{
    QSqlQuery query(m_db);
    query.prepare("SELECT value FROM metadata WHERE key = :key");
    query.bindValue(":key", "next_node_id");
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.last();
    int nodeId = query.value(0).toInt();
    return nodeId;
}

int DBManager::nextAvailableTagId()
{
    QSqlQuery query(m_db);
    query.prepare("SELECT value FROM metadata WHERE key = :key");
    query.bindValue(":key", "next_tag_id");
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.last();
    int nodeId = query.value(0).toInt();
    return nodeId;
}

void DBManager::renameNode(int id, const QString &newName)
{
    QSqlQuery query(m_db);
    query.prepare(R"(UPDATE "node_table" SET "title"=:title WHERE "id"=:id;)");
    query.bindValue(":title", newName);
    query.bindValue(":id", id);
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
}

void DBManager::renameTag(int id, const QString &newName)
{
    QSqlQuery query(m_db);
    query.prepare(R"(UPDATE "tag_table" SET "name"=:name WHERE "id"=:id;)");
    query.bindValue(":name", newName);
    query.bindValue(":id", id);
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    emit tagRenamed(id, newName);
}

void DBManager::changeTagColor(int id, const QString &newColor)
{
    QSqlQuery query(m_db);
    query.prepare(R"(UPDATE "tag_table" SET "color"=:color WHERE "id"=:id;)");
    query.bindValue(":color", newColor);
    query.bindValue(":id", id);
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    emit tagColorChanged(id, newColor);
}

/*!
 * \brief DBManager::removeNote
 * \param note
 * \return
 */
void DBManager::removeNote(const NodeData &note)
{
    if (note.parentId() == SpecialNodeID::TrashFolder) {
        QSqlQuery query(m_db);
        query.prepare(R"(DELETE FROM "node_table" )"
                      R"(WHERE id = (:id) AND node_type = (:node_type);)");
        query.bindValue(QStringLiteral(":id"), note.id());
        query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Note));
        if (!query.exec()) {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
        query.clear();
        query.prepare(R"(DELETE FROM "tag_relationship" )"
                      R"(WHERE node_id = (:id);)");
        query.bindValue(QStringLiteral(":id"), note.id());
        if (!query.exec()) {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
        if (note.nodeType() == NodeData::Note) {
            decreaseChildNotesCountFolder(SpecialNodeID::TrashFolder);
        }
    } else {
        auto trashFolder = getNode(SpecialNodeID::TrashFolder);
        moveNode(note.id(), trashFolder);
    }
}

void DBManager::removeTag(int tagId)
{
    QSqlQuery query(m_db);
    query.prepare(R"(DELETE FROM "tag_table" )"
                  R"(WHERE id = (:id);)");
    query.bindValue(QStringLiteral(":id"), tagId);
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.clear();
    query.prepare(R"(DELETE FROM "tag_relationship" )"
                  R"(WHERE tag_id = (:id);)");
    query.bindValue(QStringLiteral(":id"), tagId);
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    emit tagRemoved(tagId);
}

/*!
 * \brief DBManager::updateNote
 * \param note
 * \return
 */
bool DBManager::updateNoteContent(const NodeData &note)
{
    QSqlQuery query(m_db);
    QString emptyStr;

    int id = note.id();
    if (id == SpecialNodeID::InvalidNodeId) {
        qDebug() << "Invalid Note ID";
        return false;
    }
    qint64 epochTimeDateModified = note.lastModificationdateTime().toMSecsSinceEpoch();
    QString content = note.content().replace(QChar('\x0'), emptyStr);
    QString fullTitle = note.fullTitle().replace(QChar('\x0'), emptyStr);

    query.prepare(QStringLiteral(
            "UPDATE node_table SET modification_date = :modification_date, content = :content, "
            "title = :title, scrollbar_position = :scrollbar_position WHERE id = :id AND node_type "
            "= :node_type;"));
    query.bindValue(QStringLiteral(":modification_date"), epochTimeDateModified);
    query.bindValue(QStringLiteral(":content"), content);
    query.bindValue(QStringLiteral(":title"), fullTitle);
    query.bindValue(QStringLiteral(":id"), id);
    query.bindValue(QStringLiteral(":scrollbar_position"), note.scrollBarPosition());
    query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Note));
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    return (query.numRowsAffected() == 1);
}

QList<NodeData> DBManager::readOldNBK(const QString &fileName)
{
    QList<NodeData> noteList;
    {
        QFile file(fileName);
        file.open(QIODevice::ReadOnly);
        QDataStream in(&file);
        in.setVersion(QDataStream::Qt_5_6);

        try {
            in >> noteList;
        } catch (...) {
            // Any exception deserializing will result in an empty note list and  the user will be
            // notified
        }
        file.close();
    }
    if (noteList.isEmpty()) {
        QFile file(fileName);
        file.open(QIODevice::ReadOnly);
        QDataStream in(&file);
        in.setVersion(QDataStream::Qt_5_6);

        QList<NodeData *> nl;
        try {
            in >> nl;
        } catch (...) {
            // Any exception deserializing will result in an empty note list and  the user will be
            // notified
        }
        if (!nl.isEmpty()) {
            for (const auto &n : qAsConst(nl)) {
                noteList.append(*n);
            }
        }
        qDeleteAll(nl);
        file.close();
    }
    return noteList;
}

int DBManager::nextAvailablePosition(int parentId, NodeData::Type nodeType)
{
    QSqlQuery query(m_db);
    int relationalPosition = 0;
    if (parentId != -1) {
        query.prepare(R"(SELECT relative_position FROM "node_table" )"
                      R"(WHERE parent_id = :parent_id AND node_type = :node_type;)");
        query.bindValue(":parent_id", parentId);
        query.bindValue(":node_type", static_cast<int>(nodeType));
        bool status = query.exec();
        if (status) {
            while (query.next()) {
                if (relationalPosition <= query.value(0).toInt()) {
                    relationalPosition = query.value(0).toInt() + 1;
                }
            }
        } else {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
        query.finish();
    }
    return relationalPosition;
}

NodePath DBManager::getNodeAbsolutePath(int nodeId)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT absolute_path FROM node_table WHERE id = :id");
    query.bindValue(":id", nodeId);
    bool status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError() << query.isValid();
    }
    query.last();
    auto absolutePath = query.value(0).toString();
    return absolutePath;
}

NodeData DBManager::getNode(int nodeId)
{
    QSqlQuery query(m_db);
    query.prepare(R"(SELECT)"
                  R"("id",)"
                  R"("title",)"
                  R"("creation_date",)"
                  R"("modification_date",)"
                  R"("deletion_date",)"
                  R"("content",)"
                  R"("node_type",)"
                  R"("parent_id",)"
                  R"("relative_position",)"
                  R"("scrollbar_position",)"
                  R"("absolute_path", )"
                  R"("is_pinned_note", )"
                  R"("relative_position_an", )"
                  R"("child_notes_count" )"
                  R"(FROM node_table WHERE id=:id LIMIT 1;)");
    query.bindValue(":id", nodeId);
    bool status = query.exec();
    if (status) {
        query.next();
        NodeData node;
        node.setId(query.value(0).toInt());
        node.setFullTitle(query.value(1).toString());
        node.setCreationDateTime(QDateTime::fromMSecsSinceEpoch(query.value(2).toLongLong()));
        node.setLastModificationDateTime(
                QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong()));
        node.setDeletionDateTime(QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong()));
        node.setContent(query.value(5).toString());
        node.setNodeType(static_cast<NodeData::Type>(query.value(6).toInt()));
        node.setParentId(query.value(7).toInt());
        node.setRelativePosition(query.value(8).toInt());
        node.setScrollBarPosition(query.value(9).toInt());
        node.setAbsolutePath(query.value(10).toString());
        node.setIsPinnedNote(static_cast<bool>(query.value(11).toInt()));
        node.setRelativePosAN(query.value(13).toInt());
        node.setRelativePosAN(query.value(14).toInt());
        if (node.nodeType() == NodeData::Note) {
            node.setTagIds(getAllTagForNote(node.id()));
            QSqlQuery query2(m_db);
            query2.prepare(R"(SELECT)"
                           R"("title" )"
                           R"(FROM node_table WHERE id=:id LIMIT 1;)");
            query2.bindValue(":id", node.parentId());
            if (query2.exec()) {
                query2.next();
                node.setParentName(query2.value(0).toString());
            } else {
                qDebug() << __FUNCTION__ << __LINE__ << query2.lastError();
            }
        }
        return node;
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    qDebug() << "Can't find node with id" << nodeId;

    return NodeData();
}

void DBManager::moveFolderToTrash(const NodeData &node)
{
    QSqlQuery query(m_db);
    QString parentPath = node.absolutePath() + PATH_SEPARATOR;
    query.prepare(R"(SELECT id FROM "node_table" )"
                  R"(WHERE absolute_path like (:path_expr) || '%' AND node_type = (:node_type);)");
    query.bindValue(QStringLiteral(":path_expr"), parentPath);
    query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Note));
    bool status = query.exec();
    QSet<int> childIds;
    if (status) {
        while (query.next()) {
            childIds.insert(query.value(0).toInt());
        }
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.clear();
    auto trashFolder = getNode(SpecialNodeID::TrashFolder);
    for (const auto &id : childIds) {
        moveNode(id, trashFolder);
    }
    query.prepare(R"(DELETE FROM "node_table" )"
                  R"(WHERE absolute_path like (:path_expr) || '%' AND node_type = (:node_type);)");
    query.bindValue(QStringLiteral(":path_expr"), parentPath);
    query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Folder));
    status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    query.clear();
    query.prepare(R"(DELETE FROM "node_table" )"
                  R"(WHERE absolute_path like (:path_expr) AND node_type = (:node_type);)");
    query.bindValue(QStringLiteral(":path_expr"), node.absolutePath());
    query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Folder));
    status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
}

FolderListType DBManager::getFolderList()
{
    QMap<int, QString> result;
    QSqlQuery query(m_db);
    query.prepare(
            R"(SELECT "id", "title" FROM node_table WHERE id > 0 AND node_type = :node_type;)");
    query.bindValue(":node_type", NodeData::Folder);
    bool status = query.exec();
    if (status) {
        while (query.next()) {
            result[query.value(0).toInt()] = query.value(1).toString();
        }
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    return result;
}

void DBManager::moveNode(int nodeId, const NodeData &target)
{
    if (target.nodeType() != NodeData::Folder) {
        qDebug() << "moveNode target is not folder" << target.id();
        return;
    }
    QSqlQuery query(m_db);
    auto node = getNode(nodeId);

    QString newAbsolutePath = target.absolutePath() + PATH_SEPARATOR + QString::number(nodeId);
    if (target.id() == SpecialNodeID::TrashFolder) {
        qint64 deletionTime = QDateTime::currentMSecsSinceEpoch();
        query.prepare(QStringLiteral(
                "UPDATE node_table SET parent_id = :parent_id, absolute_path = :absolute_path, "
                "is_pinned_note = :is_pinned_note, deletion_date = :deletion_date "
                "WHERE id = :id;"));
        query.bindValue(QStringLiteral(":parent_id"), target.id());
        query.bindValue(QStringLiteral(":absolute_path"), newAbsolutePath);
        query.bindValue(QStringLiteral(":is_pinned_note"), false);
        query.bindValue(QStringLiteral(":deletion_date"), deletionTime);
        query.bindValue(QStringLiteral(":id"), nodeId);
    } else {
        query.prepare(QStringLiteral(
                "UPDATE node_table SET parent_id = :parent_id, absolute_path = :absolute_path "
                "WHERE id = :id;"));
        query.bindValue(QStringLiteral(":parent_id"), target.id());
        query.bindValue(QStringLiteral(":absolute_path"), newAbsolutePath);
        query.bindValue(QStringLiteral(":id"), nodeId);
    }
    bool status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError() << query.lastQuery();
    }

    if (node.nodeType() == NodeData::Folder) {
        QString oldAbsolutePath = node.absolutePath();
        QMap<int, QString> children;
        query.clear();
        query.prepare(R"(SELECT id, absolute_path FROM "node_table" )"
                      R"(WHERE absolute_path like (:path_expr) || '%';)");
        query.bindValue(QStringLiteral(":path_expr"), oldAbsolutePath);
        if (query.exec()) {
            while (query.next()) {
                if (query.value(0).toInt() != node.id()) {
                    children[query.value(0).toInt()] = query.value(1).toString();
                }
            }
        } else {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
        for (const auto id : children.keys()) {
            QString oldP = children[id];
            QString newP = oldP.replace(oldP.indexOf(oldAbsolutePath), oldAbsolutePath.size(),
                                        newAbsolutePath);
            if (target.id() == SpecialNodeID::TrashFolder) {
                qint64 deletionTime = QDateTime::currentMSecsSinceEpoch();
                query.prepare(QStringLiteral(
                        "UPDATE node_table SET absolute_path = :absolute_path, is_pinned_note = "
                        ":is_pinned_note, deletion_date = :deletion_date "
                        "WHERE id = :id;"));
                query.bindValue(QStringLiteral(":absolute_path"), newP);
                query.bindValue(QStringLiteral(":id"), id);
                query.bindValue(QStringLiteral(":is_pinned_note"), false);
                query.bindValue(QStringLiteral(":deletion_date"), deletionTime);
            } else {
                query.prepare(QStringLiteral("UPDATE node_table SET absolute_path = :absolute_path "
                                             "WHERE id = :id;"));
                query.bindValue(QStringLiteral(":absolute_path"), newP);
                query.bindValue(QStringLiteral(":id"), id);
            }
            if (!query.exec()) {
                qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
            }
        }
        recalculateChildNotesCount();
    } else {
        decreaseChildNotesCountFolder(node.parentId());
        if (node.parentId() != SpecialNodeID::TrashFolder
            && target.id() == SpecialNodeID::TrashFolder) {
            decreaseChildNotesCountFolder(SpecialNodeID::RootFolder);
            auto allTagInNote = getAllTagForNote(node.id());
            for (const auto &tagId : qAsConst(allTagInNote)) {
                decreaseChildNotesCountTag(tagId);
            }
        } else if (node.parentId() == SpecialNodeID::TrashFolder
                   && target.id() != SpecialNodeID::TrashFolder) {
            increaseChildNotesCountFolder(SpecialNodeID::RootFolder);
            auto allTagInNote = getAllTagForNote(node.id());
            for (const auto &tagId : qAsConst(allTagInNote)) {
                increaseChildNotesCountTag(tagId);
            }
        }
        increaseChildNotesCountFolder(target.id());
    }
}

void DBManager::searchForNotes(const QString &keyword, const ListViewInfo &inf)
{
    QVector<NodeData> nodeList;
    QSqlQuery query(m_db);
    if (!inf.isInTag && inf.parentFolderId == SpecialNodeID::RootFolder) {
        query.prepare(R"(SELECT )"
                      R"("id",)"
                      R"("title",)"
                      R"("creation_date",)"
                      R"("modification_date",)"
                      R"("deletion_date",)"
                      R"("content",)"
                      R"("node_type",)"
                      R"("parent_id",)"
                      R"("relative_position", )"
                      R"("scrollbar_position",)"
                      R"("absolute_path", )"
                      R"("is_pinned_note", )"
                      R"("relative_position_an", )"
                      R"("child_notes_count" )"
                      R"(FROM node_table )"
                      R"(WHERE node_type = (:node_type) AND parent_id != (:parent_id) )"
                      R"(AND content like  '%' || (:search_expr) || '%';)");
        query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Note));
        query.bindValue(QStringLiteral(":parent_id"), static_cast<int>(SpecialNodeID::TrashFolder));
        query.bindValue(QStringLiteral(":search_expr"), keyword);

        bool status = query.exec();
        if (status) {
            while (query.next()) {
                NodeData node;
                node.setId(query.value(0).toInt());
                node.setFullTitle(query.value(1).toString());
                node.setCreationDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(2).toLongLong()));
                node.setLastModificationDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong()));
                node.setDeletionDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong()));
                node.setContent(query.value(5).toString());
                node.setNodeType(static_cast<NodeData::Type>(query.value(6).toInt()));
                node.setParentId(query.value(7).toInt());
                node.setRelativePosition(query.value(8).toInt());
                node.setScrollBarPosition(query.value(9).toInt());
                node.setAbsolutePath(query.value(10).toString());
                node.setIsPinnedNote(static_cast<bool>(query.value(11).toInt()));
                node.setRelativePosAN(query.value(13).toInt());
                node.setChildNotesCount(query.value(14).toInt());
                node.setTagIds(getAllTagForNote(node.id()));
                auto p = getNode(node.parentId());
                node.setParentName(p.fullTitle());
                nodeList.append(node);
            }
        } else {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
    } else if (!inf.isInTag) {
        query.prepare(R"(SELECT )"
                      R"("id",)"
                      R"("title",)"
                      R"("creation_date",)"
                      R"("modification_date",)"
                      R"("deletion_date",)"
                      R"("content",)"
                      R"("node_type",)"
                      R"("parent_id",)"
                      R"("relative_position", )"
                      R"("scrollbar_position",)"
                      R"("absolute_path", )"
                      R"("is_pinned_note", )"
                      R"("relative_position_an", )"
                      R"("child_notes_count" )"
                      R"(FROM node_table )"
                      R"(WHERE node_type = (:node_type) AND parent_id == (:parent_id) )"
                      R"(AND content like  '%' || (:search_expr) || '%';)");
        query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Note));
        query.bindValue(QStringLiteral(":parent_id"), static_cast<int>(inf.parentFolderId));
        query.bindValue(QStringLiteral(":search_expr"), keyword);

        bool status = query.exec();
        if (status) {
            while (query.next()) {
                NodeData node;
                node.setId(query.value(0).toInt());
                node.setFullTitle(query.value(1).toString());
                node.setCreationDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(2).toLongLong()));
                node.setLastModificationDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong()));
                node.setDeletionDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong()));
                node.setContent(query.value(5).toString());
                node.setNodeType(static_cast<NodeData::Type>(query.value(6).toInt()));
                node.setParentId(query.value(7).toInt());
                node.setRelativePosition(query.value(8).toInt());
                node.setScrollBarPosition(query.value(9).toInt());
                node.setAbsolutePath(query.value(10).toString());
                node.setIsPinnedNote(static_cast<bool>(query.value(11).toInt()));
                node.setRelativePosAN(query.value(13).toInt());
                node.setChildNotesCount(query.value(14).toInt());
                node.setTagIds(getAllTagForNote(node.id()));
                auto p = getNode(node.parentId());
                node.setParentName(p.fullTitle());
                nodeList.append(node);
            }
        } else {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
    } else if (inf.isInTag) {
        QVector<QSet<int>> nds;
        for (const auto &tagId : inf.currentTagList) {
            QSet<int> nd;
            query.prepare(R"(SELECT "node_id" FROM tag_relationship WHERE tag_id = :tag_id;)");
            query.bindValue(QStringLiteral(":tag_id"), tagId);
            bool status = query.exec();
            if (status) {
                while (query.next()) {
                    nd.insert(query.value(0).toInt());
                }
            } else {
                qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
            }
            nds.append(nd);
        }
        if (nds.size() == 0) {
            emit notesListReceived(nodeList, inf);
            return;
        }
        QSet<int> noteIds;
        int nds_id = 0;
        for (int i = 1; i < nds.size(); ++i) {
            if (nds[i].size() < nds[nds_id].size()) {
                nds_id = i;
            }
        }
        for (const int id : qAsConst(nds[nds_id])) {
            bool ok = true;
            for (int i = 0; i < nds.size(); ++i) {
                if (i == nds_id) {
                    continue;
                }
                if (!nds[i].contains(id)) {
                    ok = false;
                }
            }
            if (ok) {
                noteIds.insert(id);
            }
        }
        for (const auto &id : noteIds) {
            NodeData node = getNode(id);
            if (node.id() != SpecialNodeID::InvalidNodeId && node.nodeType() == NodeData::Note
                && node.content().contains(keyword)) {
                nodeList.append(node);
            } else {
                qDebug() << __FUNCTION__ << "Note with id" << id << "is not valid";
            }
        }
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << "not supported";
    }
    ListViewInfo _inf = inf;
    _inf.isInSearch = true;
    std::sort(nodeList.begin(), nodeList.end(), [](const NodeData &a, const NodeData &b) -> bool {
        return a.lastModificationdateTime() > b.lastModificationdateTime();
    });
    emit notesListReceived(nodeList, _inf);
}

void DBManager::clearSearch(const ListViewInfo &inf)
{
    if (inf.isInTag) {
        onNotesListInTagsRequested(inf.currentTagList, inf.needCreateNewNote, inf.scrollToId);
    } else {
        if (inf.parentFolderId == SpecialNodeID::RootFolder) {
            onNotesListInFolderRequested(inf.parentFolderId, true, inf.needCreateNewNote,
                                         inf.scrollToId);
        } else {
            onNotesListInFolderRequested(inf.parentFolderId, false, inf.needCreateNewNote,
                                         inf.scrollToId);
        }
    }
}

void DBManager::updateRelPosNode(int nodeId, int relPos)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE node_table SET relative_position = :relative_position "
                                 "WHERE id = :id;"));
    query.bindValue(QStringLiteral(":relative_position"), relPos);
    query.bindValue(QStringLiteral(":id"), nodeId);
    bool status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
}

void DBManager::updateRelPosTag(int tagId, int relPos)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE tag_table SET relative_position = :relative_position "
                                 "WHERE id = :id;"));
    query.bindValue(QStringLiteral(":relative_position"), relPos);
    query.bindValue(QStringLiteral(":id"), tagId);
    bool status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
}

void DBManager::updateRelPosPinnedNote(int nodeId, int relPos)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE node_table SET relative_position = :relative_position "
                                 "WHERE id = :id AND node_type=:node_type;"));
    query.bindValue(QStringLiteral(":relative_position"), relPos);
    query.bindValue(QStringLiteral(":id"), nodeId);
    query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Type::Note));
    bool status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
}

void DBManager::updateRelPosPinnedNoteAN(int nodeId, int relPos)
{
    QSqlQuery query(m_db);
    query.prepare(
            QStringLiteral("UPDATE node_table SET relative_position_an = :relative_position_an "
                           "WHERE id = :id AND node_type=:node_type;"));
    query.bindValue(QStringLiteral(":relative_position_an"), relPos);
    query.bindValue(QStringLiteral(":id"), nodeId);
    query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Type::Note));
    bool status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
}

void DBManager::setNoteIsPinned(int noteId, bool isPinned)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE node_table SET is_pinned_note = :is_pinned_note "
                                 "WHERE id = :id AND node_type=:node_type;"));
    query.bindValue(QStringLiteral(":is_pinned_note"), isPinned);
    query.bindValue(QStringLiteral(":id"), noteId);
    query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Type::Note));
    bool status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
}

NodeData DBManager::getChildNotesCountFolder(int folderId)
{
    NodeData d;
    d.setNodeType(NodeData::Folder);
    d.setId(folderId);
    QSqlQuery query(m_db);
    query.prepare(R"(SELECT child_notes_count, absolute_path  FROM "node_table" WHERE id=:id)");
    query.bindValue(QStringLiteral(":id"), folderId);
    bool status = query.exec();
    int childNotesCount = 0;
    QString absPath;
    if (status) {
        query.next();
        childNotesCount = query.value(0).toInt();
        absPath = query.value(1).toString();
    } else {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    d.setChildNotesCount(childNotesCount);
    d.setAbsolutePath(absPath);
    return d;
}

void DBManager::onNodeTagTreeRequested()
{
    NodeTagTreeData d;
    d.nodeTreeData = getAllFolders();
    d.tagTreeData = getAllTagInfo();
    emit nodesTagTreeReceived(d);
}

/*!
 * \brief DBManager::onNotesListRequested
 */
void DBManager::onNotesListInFolderRequested(int parentID, bool isRecursive, bool newNote,
                                             int scrollToId)
{
    QVector<NodeData> nodeList;
    QSqlQuery query(m_db);
    if (parentID == SpecialNodeID::RootFolder) {
        query.prepare(R"(SELECT )"
                      R"("id",)"
                      R"("title",)"
                      R"("creation_date",)"
                      R"("modification_date",)"
                      R"("deletion_date",)"
                      R"("content",)"
                      R"("node_type",)"
                      R"("parent_id",)"
                      R"("relative_position",)"
                      R"("scrollbar_position",)"
                      R"("absolute_path", )"
                      R"("is_pinned_note", )"
                      R"("relative_position_an", )"
                      R"("child_notes_count" )"
                      R"(FROM node_table )"
                      R"(WHERE node_type = (:node_type) AND parent_id != (:parent_id);)");
        query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Note));
        query.bindValue(QStringLiteral(":parent_id"), static_cast<int>(SpecialNodeID::TrashFolder));

        bool status = query.exec();
        if (status) {
            while (query.next()) {
                NodeData node;
                node.setId(query.value(0).toInt());
                node.setFullTitle(query.value(1).toString());
                node.setCreationDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(2).toLongLong()));
                node.setLastModificationDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong()));
                node.setDeletionDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong()));
                node.setContent(query.value(5).toString());
                node.setNodeType(static_cast<NodeData::Type>(query.value(6).toInt()));
                node.setParentId(query.value(7).toInt());
                node.setRelativePosition(query.value(8).toInt());
                node.setScrollBarPosition(query.value(9).toInt());
                node.setAbsolutePath(query.value(10).toString());
                node.setIsPinnedNote(static_cast<bool>(query.value(11).toInt()));
                node.setRelativePosAN(query.value(13).toInt());
                node.setChildNotesCount(query.value(14).toInt());
                node.setTagIds(getAllTagForNote(node.id()));
                auto p = getNode(node.parentId());
                node.setParentName(p.fullTitle());
                nodeList.append(node);
            }
        } else {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
    } else if (!isRecursive) {
        query.prepare(R"(SELECT )"
                      R"("id",)"
                      R"("title",)"
                      R"("creation_date",)"
                      R"("modification_date",)"
                      R"("deletion_date",)"
                      R"("content",)"
                      R"("node_type",)"
                      R"("parent_id",)"
                      R"("relative_position", )"
                      R"("scrollbar_position",)"
                      R"("absolute_path", )"
                      R"("is_pinned_note", )"
                      R"("relative_position_an", )"
                      R"("child_notes_count" )"
                      R"(FROM node_table )"
                      R"(WHERE parent_id = (:parent_id) AND node_type = (:node_type);)");
        query.bindValue(QStringLiteral(":parent_id"), parentID);
        query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Note));
        bool status = query.exec();
        if (status) {
            while (query.next()) {
                NodeData node;
                node.setId(query.value(0).toInt());
                node.setFullTitle(query.value(1).toString());
                node.setCreationDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(2).toLongLong()));
                node.setLastModificationDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong()));
                node.setDeletionDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong()));
                node.setContent(query.value(5).toString());
                node.setNodeType(static_cast<NodeData::Type>(query.value(6).toInt()));
                node.setParentId(query.value(7).toInt());
                node.setRelativePosition(query.value(8).toInt());
                node.setScrollBarPosition(query.value(9).toInt());
                node.setAbsolutePath(query.value(10).toString());
                node.setIsPinnedNote(static_cast<bool>(query.value(11).toInt()));
                node.setRelativePosAN(query.value(13).toInt());
                node.setChildNotesCount(query.value(14).toInt());
                node.setTagIds(getAllTagForNote(node.id()));
                nodeList.append(node);
            }
        } else {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
    } else {
        auto parentPath = getNodeAbsolutePath(parentID).path() + PATH_SEPARATOR;
        query.prepare(
                R"(SELECT )"
                R"("id",)"
                R"("title",)"
                R"("creation_date",)"
                R"("modification_date",)"
                R"("deletion_date",)"
                R"("content",)"
                R"("node_type",)"
                R"("parent_id",)"
                R"("relative_position", )"
                R"("scrollbar_position",)"
                R"("absolute_path", )"
                R"("is_pinned_note", )"
                R"("relative_position_an", )"
                R"("child_notes_count" )"
                R"(FROM node_table )"
                R"(WHERE absolute_path like (:path_expr) || '%' AND node_type = (:node_type);)");
        query.bindValue(QStringLiteral(":path_expr"), parentPath);
        query.bindValue(QStringLiteral(":node_type"), static_cast<int>(NodeData::Note));

        bool status = query.exec();
        if (status) {
            while (query.next()) {
                NodeData node;
                node.setId(query.value(0).toInt());
                node.setFullTitle(query.value(1).toString());
                node.setCreationDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(2).toLongLong()));
                node.setLastModificationDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong()));
                node.setDeletionDateTime(
                        QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong()));
                node.setContent(query.value(5).toString());
                node.setNodeType(static_cast<NodeData::Type>(query.value(6).toInt()));
                node.setParentId(query.value(7).toInt());
                node.setRelativePosition(query.value(8).toInt());
                node.setScrollBarPosition(query.value(9).toInt());
                node.setAbsolutePath(query.value(10).toString());
                node.setIsPinnedNote(static_cast<bool>(query.value(11).toInt()));
                node.setRelativePosAN(query.value(13).toInt());
                node.setChildNotesCount(query.value(14).toInt());
                node.setTagIds(getAllTagForNote(node.id()));
                nodeList.append(node);
            }
        } else {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
    }
    ListViewInfo inf;
    inf.isInSearch = false;
    inf.isInTag = false;
    inf.parentFolderId = parentID;
    inf.currentNotesId = { SpecialNodeID::InvalidNodeId };
    inf.needCreateNewNote = newNote;
    inf.scrollToId = scrollToId;
    std::sort(nodeList.begin(), nodeList.end(), [](const NodeData &a, const NodeData &b) -> bool {
        return a.lastModificationdateTime() > b.lastModificationdateTime();
    });
    emit notesListReceived(nodeList, inf);
}

void DBManager::onNotesListInTagsRequested(const QSet<int> &tagIds, bool newNote, int scrollToId)
{
    QVector<NodeData> nodeList;
    QVector<QSet<int>> nds;
    ListViewInfo inf;
    inf.isInSearch = false;
    inf.isInTag = true;
    inf.currentTagList = tagIds;
    inf.currentNotesId = { SpecialNodeID::InvalidNodeId };
    inf.needCreateNewNote = newNote;
    inf.scrollToId = scrollToId;
    for (const auto &tagId : tagIds) {
        QSet<int> nd;
        QSqlQuery query(m_db);
        query.prepare(R"(SELECT "node_id" FROM tag_relationship WHERE tag_id = :tag_id;)");
        query.bindValue(QStringLiteral(":tag_id"), tagId);
        bool status = query.exec();
        if (status) {
            while (query.next()) {
                nd.insert(query.value(0).toInt());
            }
        } else {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
        nds.append(nd);
    }
    if (nds.size() == 0) {
        emit notesListReceived(nodeList, inf);
        return;
    }
    QSet<int> noteIds;
    int nds_id = 0;
    for (int i = 1; i < nds.size(); ++i) {
        if (nds[i].size() < nds[nds_id].size()) {
            nds_id = i;
        }
    }
    for (const int id : qAsConst(nds[nds_id])) {
        bool ok = true;
        for (int i = 0; i < nds.size(); ++i) {
            if (i == nds_id) {
                continue;
            }
            if (!nds[i].contains(id)) {
                ok = false;
            }
        }
        if (ok) {
            noteIds.insert(id);
        }
    }
    for (const auto &id : noteIds) {
        NodeData node = getNode(id);
        if (node.id() != SpecialNodeID::InvalidNodeId && node.nodeType() == NodeData::Note) {
            nodeList.append(node);
        } else {
            qDebug() << __FUNCTION__ << "Note with id" << id << "is not valid";
        }
    }
    std::sort(nodeList.begin(), nodeList.end(), [](const NodeData &a, const NodeData &b) -> bool {
        return a.lastModificationdateTime() > b.lastModificationdateTime();
    });
    emit notesListReceived(nodeList, inf);
}

/*!
 * \brief DBManager::onOpenDBManagerRequested
 * \param path
 * \param doCreate
 */
void DBManager::onOpenDBManagerRequested(const QString &path, bool doCreate)
{
    open(path, doCreate);
    emit databaseOpened();
}

/*!
 * \brief DBManager::onCreateUpdateRequested
 * \param note
 */
void DBManager::onCreateUpdateRequestedNoteContent(const NodeData &note)
{
    if (note.nodeType() != NodeData::Note) {
        qDebug() << "Wrong node type";
        return;
    }
    bool exists = isNodeExist(note);

    if (exists) {
        updateNoteContent(note);
    } else {
        addNode(note);
    }
}

/*!
 * \brief DBManager::onImportNotesRequested
 * \param noteList
 */
void DBManager::onImportNotesRequested(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << __FUNCTION__ << __LINE__ << "fail to open file";
        return;
    }
    auto magic_header = file.read(16);
    file.close();
    if (QString::fromUtf8(magic_header).startsWith(QStringLiteral("SQLite format 3"))) {
        qDebug() << __FUNCTION__;
        auto outside_db = QSqlDatabase::addDatabase("QSQLITE", OUTSIDE_DATABASE_NAME);
        outside_db.setDatabaseName(fileName);
        if (!outside_db.open()) {
            qDebug() << __FUNCTION__ << "Error: connection with database fail";
            return;
        } else {
            qDebug() << __FUNCTION__ << "Database: connection ok";
        }

        QMap<int, int> folderIdMap, noteIdMap, tagIdMap;
        {
            QVector<TagData> tagList;
            QSqlQuery out_qr(outside_db);
            out_qr.prepare(
                    R"(SELECT "id","name","color","relative_position","child_notes_count" FROM tag_table;)");
            bool status = out_qr.exec();
            if (status) {
                while (out_qr.next()) {
                    TagData tag;
                    tag.setId(out_qr.value(0).toInt());
                    tag.setName(out_qr.value(1).toString());
                    tag.setColor(out_qr.value(2).toString());
                    tag.setRelativePosition(out_qr.value(3).toInt());
                    tag.setChildNotesCount(out_qr.value(4).toInt());
                    tagList.append(tag);
                }
            } else {
                qDebug() << __FUNCTION__ << __LINE__ << out_qr.lastError();
            }
            out_qr.finish();
            std::sort(tagList.begin(), tagList.end(),
                      [](auto a, auto b) { return a.relativePosition() < b.relativePosition(); });
            for (const auto &tag : qAsConst(tagList)) {
                QSqlQuery qr(m_db);
                qr.prepare(R"(SELECT "id" FROM tag_table WHERE name = :name AND color = :color;)");
                qr.bindValue(QStringLiteral(":name"), tag.name());
                qr.bindValue(QStringLiteral(":color"), tag.color());
                QVector<int> ids;
                if (qr.exec()) {
                    while (qr.next()) {
                        ids.append(qr.value(0).toInt());
                    }
                } else {
                    qDebug() << __FUNCTION__ << __LINE__ << qr.lastError();
                }
                qr.finish();
                if (ids.isEmpty()) {
                    ids.append(addTag(tag));
                }
                tagIdMap[tag.id()] = ids[0];
            }
        }
        {
            folderIdMap[SpecialNodeID::RootFolder] = SpecialNodeID::RootFolder;
            folderIdMap[SpecialNodeID::TrashFolder] = SpecialNodeID::TrashFolder;
            folderIdMap[SpecialNodeID::DefaultNotesFolder] = SpecialNodeID::DefaultNotesFolder;
            QSqlQuery out_qr(outside_db);
            out_qr.prepare(R"(SELECT)"
                           R"("id",)"
                           R"("title",)"
                           R"("creation_date",)"
                           R"("modification_date",)"
                           R"("deletion_date",)"
                           R"("content",)"
                           R"("node_type",)"
                           R"("parent_id",)"
                           R"("relative_position",)"
                           R"("absolute_path" )"
                           R"(FROM node_table WHERE node_type=:node_type;)");
            out_qr.bindValue(":node_type", static_cast<int>(NodeData::Type::Folder));
            bool status = out_qr.exec();
            QMap<int, NodeData> nodeList;
            if (status) {
                while (out_qr.next()) {
                    NodeData node;
                    node.setId(out_qr.value(0).toInt());
                    node.setFullTitle(out_qr.value(1).toString());
                    node.setCreationDateTime(
                            QDateTime::fromMSecsSinceEpoch(out_qr.value(2).toLongLong()));
                    node.setLastModificationDateTime(
                            QDateTime::fromMSecsSinceEpoch(out_qr.value(3).toLongLong()));
                    node.setDeletionDateTime(
                            QDateTime::fromMSecsSinceEpoch(out_qr.value(4).toLongLong()));
                    node.setContent(out_qr.value(5).toString());
                    node.setNodeType(static_cast<NodeData::Type>(out_qr.value(6).toInt()));
                    node.setParentId(out_qr.value(7).toInt());
                    node.setRelativePosition(out_qr.value(8).toInt());
                    node.setAbsolutePath(out_qr.value(9).toString());
                    nodeList[node.id()] = node;
                }
                auto matchFolderFunc = [&](int id, const auto &matchFolderFunctor) {
                    if (!nodeList.contains(id)) {
                        qDebug() << __FUNCTION__ << __LINE__ << "node not found";
                        return;
                    }
                    if (folderIdMap.contains(id)) {
                        return;
                    }
                    auto node = nodeList[id];
                    if (folderIdMap.contains(node.parentId())) {
                        QSqlQuery qr(m_db);
                        qr.prepare(
                                R"(SELECT "id" FROM node_table WHERE title = :title AND node_type = :node_type AND parent_id = :parent_id;)");
                        qr.bindValue(QStringLiteral(":title"), node.fullTitle());
                        qr.bindValue(QStringLiteral(":node_type"),
                                     static_cast<int>(NodeData::Folder));
                        qr.bindValue(QStringLiteral(":parent_id"), folderIdMap[node.parentId()]);
                        QVector<int> ids;
                        if (qr.exec()) {
                            while (qr.next()) {
                                ids.append(qr.value(0).toInt());
                            }
                        } else {
                            qDebug() << __FUNCTION__ << __LINE__ << qr.lastError();
                        }
                        qr.finish();
                        if (!ids.isEmpty()) {
                            folderIdMap[id] = ids[0];
                        } else {
                            node.setParentId(folderIdMap[node.parentId()]);
                            int newId = addNode(node);
                            folderIdMap[id] = newId;
                        }
                    } else {
                        auto prL = NodePath(node.absolutePath()).separate();
                        for (const auto &pr : qAsConst(prL)) {
                            matchFolderFunctor(pr.toInt(), matchFolderFunctor);
                        }
                    }
                };

                struct Folder
                {
                    int id;
                    int parentId;
                    std::vector<Folder *> children;
                };
                Folder *rootFolder = nullptr;
                QHash<int, Folder> needImportFolderMap;
                for (const auto &node : qAsConst(nodeList)) {
                    Folder f;
                    f.id = node.id();
                    f.parentId = node.parentId();
                    needImportFolderMap[node.id()] = f;
                    if (f.id == SpecialNodeID::RootFolder) {
                        rootFolder = &needImportFolderMap[node.id()];
                    }
                }
                if (rootFolder) {
                    for (const auto &folder : qAsConst(needImportFolderMap)) {
                        if (folder.id != SpecialNodeID::RootFolder) {
                            needImportFolderMap[folder.parentId].children.push_back(
                                    &needImportFolderMap[folder.id]);
                        }
                    }
                    auto sortChildFunc = [&](Folder *f) {
                        std::sort(f->children.begin(), f->children.end(),
                                  [&](Folder *f1, Folder *f2) {
                                      return nodeList[f1->id].relativePosition()
                                              < nodeList[f2->id].relativePosition();
                                  });
                    };
                    auto sortFunc = [&](Folder *f, const auto &sf) -> void {
                        sortChildFunc(f);
                        for (auto c : f->children) {
                            sf(c, sf);
                        }
                    };
                    sortFunc(rootFolder, sortFunc);
                    auto matchFunc = [&](Folder *f, const auto &mf) -> void {
                        matchFolderFunc(f->id, matchFolderFunc);
                        for (auto c : f->children) {
                            mf(c, mf);
                        }
                    };
                    matchFunc(rootFolder, matchFunc);
                } else {
                    qDebug() << __FUNCTION__ << "Error while keeping folder position";
                    for (const auto &node : qAsConst(nodeList)) {
                        matchFolderFunc(node.id(), matchFolderFunc);
                    }
                }
            } else {
                qDebug() << __FUNCTION__ << __LINE__ << out_qr.lastError();
            }
        }
        {
            QSqlQuery out_qr(outside_db);
            out_qr.prepare(R"(SELECT)"
                           R"("id",)"
                           R"("title",)"
                           R"("creation_date",)"
                           R"("modification_date",)"
                           R"("deletion_date",)"
                           R"("content",)"
                           R"("node_type",)"
                           R"("parent_id",)"
                           R"("relative_position",)"
                           R"("scrollbar_position",)"
                           R"("absolute_path", )"
                           R"("is_pinned_note", )"
                           R"("relative_position_an" )"
                           R"(FROM node_table WHERE node_type=:node_type;)");

            out_qr.bindValue(":node_type", static_cast<int>(NodeData::Type::Note));
            bool status = out_qr.exec();
            QVector<NodeData> nodeList;
            QMap<int, std::pair<NodeData, int>> parents;
            for (const auto &id : qAsConst(folderIdMap)) {
                if (parents.contains(id)) {
                    continue;
                }
                auto node = getNode(id);
                auto rel = nextAvailablePosition(id, NodeData::Note);
                parents[id] = std::make_pair(node, rel);
            }
            if (status) {
                while (out_qr.next()) {
                    NodeData node;
                    node.setId(out_qr.value(0).toInt());
                    node.setFullTitle(out_qr.value(1).toString());
                    node.setCreationDateTime(
                            QDateTime::fromMSecsSinceEpoch(out_qr.value(2).toLongLong()));
                    node.setLastModificationDateTime(
                            QDateTime::fromMSecsSinceEpoch(out_qr.value(3).toLongLong()));
                    node.setDeletionDateTime(
                            QDateTime::fromMSecsSinceEpoch(out_qr.value(4).toLongLong()));
                    node.setContent(out_qr.value(5).toString());
                    node.setNodeType(static_cast<NodeData::Type>(out_qr.value(6).toInt()));
                    node.setParentId(out_qr.value(7).toInt());
                    node.setRelativePosition(out_qr.value(8).toInt());
                    node.setScrollBarPosition(out_qr.value(9).toInt());
                    node.setAbsolutePath(out_qr.value(10).toString());
                    node.setIsPinnedNote(static_cast<bool>(out_qr.value(11).toInt()));
                    node.setRelativePosAN(out_qr.value(13).toInt());
                    node.setChildNotesCount(out_qr.value(14).toInt());
                    node.setTagIds(getAllTagForNote(node.id()));
                    nodeList.append(node);
                }
                m_db.transaction();
                auto nodeId = nextAvailableNodeId();
                for (auto node : qAsConst(nodeList)) {
                    if (folderIdMap.contains(node.parentId())
                        && parents.contains(folderIdMap[node.parentId()])) {
                        auto parentId = folderIdMap[node.parentId()];
                        auto parent = parents[parentId].first;
                        auto oldId = node.id();
                        node.setId(nodeId);
                        node.setRelativePosition(parents[parentId].second);
                        node.setAbsolutePath(parent.absolutePath() + PATH_SEPARATOR
                                             + QString::number(nodeId));
                        node.setParentId(parentId);
                        addNodePreComputed(node);
                        ++nodeId;
                        parents[parentId].second = node.relativePosition() + 1;
                        noteIdMap[oldId] = node.id();
                    } else {
                        qDebug() << __FUNCTION__ << __LINE__ << "can't find parent for note";
                    }
                }
                QSqlQuery query(m_db);
                query.prepare(
                        R"(UPDATE "metadata" SET "value"=:value WHERE "key"='next_node_id';)");
                query.bindValue(":value", nodeId + 1);
                if (!query.exec()) {
                    qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
                }
                m_db.commit();
            } else {
                qDebug() << __FUNCTION__ << __LINE__ << out_qr.lastError();
            }
        }
        {
            QSqlQuery out_qr(outside_db);
            out_qr.prepare(R"(SELECT "tag_id", "node_id" FROM tag_relationship)");
            bool status = out_qr.exec();
            QVector<std::pair<int, int>> tagRela;
            if (status) {
                while (out_qr.next()) {
                    tagRela.append(
                            std::make_pair(out_qr.value(0).toInt(), out_qr.value(1).toInt()));
                }
                m_db.transaction();
                for (const auto &rel : qAsConst(tagRela)) {
                    if (tagIdMap.contains(rel.first) && noteIdMap.contains(rel.second)) {
                        addNoteToTag(noteIdMap[rel.second], tagIdMap[rel.first]);
                    } else {
                        qDebug() << __FUNCTION__ << __LINE__ << "tag relationship is not valid";
                    }
                }
                m_db.commit();
            } else {
                qDebug() << __FUNCTION__ << __LINE__ << out_qr.lastError();
            }
        }
        {
            outside_db.close();
            outside_db = QSqlDatabase::database();
        }
    } else {
        auto noteList = readOldNBK(fileName);
        if (noteList.isEmpty()) {
            emit showErrorMessage(tr("Invalid file"), "Please select a valid notes export file");
        } else {
            auto defaultNoteFolder = getNode(SpecialNodeID::DefaultNotesFolder);
            int nodeId = nextAvailableNodeId();
            int notePos = nextAvailablePosition(defaultNoteFolder.id(), NodeData::Note);
            QString parentAbsPath = defaultNoteFolder.absolutePath();
            m_db.transaction();
            for (auto &note : noteList) {
                note.setId(nodeId);
                note.setRelativePosition(notePos);
                note.setAbsolutePath(parentAbsPath + PATH_SEPARATOR + QString::number(nodeId));
                note.setNodeType(NodeData::Note);
                note.setParentId(SpecialNodeID::DefaultNotesFolder);
                note.setParentName("Notes");
                note.setIsTempNote(false);
                addNodePreComputed(note);
                ++nodeId;
                ++notePos;
            }
            QSqlQuery query(m_db);
            query.prepare(R"(UPDATE "metadata" SET "value"=:value WHERE "key"='next_node_id';)");
            query.bindValue(":value", nodeId + 1);
            if (!query.exec()) {
                qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
            }
            m_db.commit();
        }
    }
    recalculateChildNotesCount();
    onNodeTagTreeRequested();
}

/*!
 * \brief DBManager::onRestoreNotesRequested
 * \param noteList
 */
void DBManager::onRestoreNotesRequested(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << __FUNCTION__ << __LINE__ << "fail to open file";
        return;
    }
    auto magic_header = file.read(16);
    file.close();
    if (QString::fromUtf8(magic_header).startsWith(QStringLiteral("SQLite format 3"))) {
        {
            m_db.close();
            m_db = QSqlDatabase::database();
        }
        QSqlDatabase::removeDatabase(DEFAULT_DATABASE_NAME);
        QFile::remove(m_dbpath);
        if (!QFile::copy(fileName, m_dbpath)) {
            qDebug() << __FUNCTION__ << "Can't import notes";
        };
        open(m_dbpath, false);
    } else {
        auto noteList = readOldNBK(fileName);
        if (noteList.isEmpty()) {
            emit showErrorMessage(tr("Invalid file"), "Please select a valid notes export file");
        } else {
            {
                m_db.close();
                m_db = QSqlDatabase::database();
            }
            QSqlDatabase::removeDatabase(DEFAULT_DATABASE_NAME);
            QFile::remove(m_dbpath);
            open(m_dbpath, true);
            auto defaultNoteFolder = getNode(SpecialNodeID::DefaultNotesFolder);
            int nodeId = nextAvailableNodeId();
            int notePos = nextAvailablePosition(defaultNoteFolder.id(), NodeData::Note);
            QString parentAbsPath = defaultNoteFolder.absolutePath();
            m_db.transaction();
            for (auto &note : noteList) {
                note.setId(nodeId);
                note.setRelativePosition(notePos);
                note.setAbsolutePath(parentAbsPath + PATH_SEPARATOR + QString::number(nodeId));
                note.setNodeType(NodeData::Note);
                note.setParentId(defaultNoteFolder.id());
                note.setParentName("Notes");
                note.setIsTempNote(false);
                addNodePreComputed(note);
                ++nodeId;
                ++notePos;
            }
            QSqlQuery query(m_db);
            query.prepare(R"(UPDATE "metadata" SET "value"=:value WHERE "key"='next_node_id';)");
            query.bindValue(":value", nodeId + 1);
            if (!query.exec()) {
                qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
            }
            m_db.commit();
        }
    }
    recalculateChildNotesCount();
    onNodeTagTreeRequested();
}

/*!
 * \brief DBManager::onExportNotesRequested
 * \param fileName
 */
void DBManager::onExportNotesRequested(const QString &fileName)
{
    QSqlQuery query(m_db);
    query.prepare("BEGIN IMMEDIATE;");
    bool status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    QFile::remove(fileName);
    if (!QFile::copy(m_dbpath, fileName)) {
        qDebug() << __FUNCTION__ << "Can't export notes";
    };

    query.prepare("ROLLBACK;");
    status = query.exec();
    if (!status) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
}

/*!
 * \brief DBManager::onMigrateNotesRequested
 * \param noteList
 */
void DBManager::onMigrateNotesFromV0_9_0Requested(QVector<NodeData> &noteList)
{
    auto defaultNoteFolder = getNode(SpecialNodeID::DefaultNotesFolder);
    int nodeId = nextAvailableNodeId();
    int notePos = nextAvailablePosition(defaultNoteFolder.id(), NodeData::Note);
    QString parentAbsPath = defaultNoteFolder.absolutePath();

    m_db.transaction();
    for (auto &note : noteList) {
        note.setId(nodeId);
        note.setRelativePosition(notePos);
        note.setAbsolutePath(parentAbsPath + PATH_SEPARATOR + QString::number(nodeId));
        note.setNodeType(NodeData::Note);
        note.setParentId(defaultNoteFolder.id());
        note.setParentName("Notes");
        note.setIsTempNote(false);
        addNodePreComputed(note);
        ++nodeId;
        ++notePos;
    }
    QSqlQuery query(m_db);
    query.prepare(R"(UPDATE "metadata" SET "value"=:value WHERE "key"='next_node_id';)");
    query.bindValue(":value", nodeId + 1);
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    m_db.commit();
    recalculateChildNotesCount();
}

/*!
 * \brief DBManager::onMigrateTrashRequested
 * \param noteList
 */
void DBManager::onMigrateTrashFrom0_9_0Requested(QVector<NodeData> &noteList)
{
    auto trashFolder = getNode(SpecialNodeID::TrashFolder);
    int nodeId = nextAvailableNodeId();
    int notePos = nextAvailablePosition(trashFolder.id(), NodeData::Note);
    QString parentAbsPath = trashFolder.absolutePath();

    m_db.transaction();
    for (auto &note : noteList) {
        note.setId(nodeId);
        note.setRelativePosition(notePos);
        note.setAbsolutePath(parentAbsPath + PATH_SEPARATOR + QString::number(nodeId));
        note.setNodeType(NodeData::Note);
        note.setParentId(trashFolder.id());
        note.setParentName("Trash");
        note.setIsTempNote(false);
        addNodePreComputed(note);
        ++nodeId;
        ++notePos;
    }
    QSqlQuery query(m_db);
    query.prepare(R"(UPDATE "metadata" SET "value"=:value WHERE "key"='next_node_id';)");
    query.bindValue(":value", nodeId + 1);
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    m_db.commit();
    recalculateChildNotesCount();
}

void DBManager::onMigrateNotesFrom1_5_0Requested(const QString &fileName)
{
    auto old_db = QSqlDatabase::addDatabase("QSQLITE", OUTSIDE_DATABASE_NAME);
    old_db.setDatabaseName(fileName);
    if (!old_db.open()) {
        qDebug() << __FUNCTION__ << "Error: connection with database fail";
        return;
    } else {
        qDebug() << __FUNCTION__ << "Database: connection ok";
    }
    QVector<NodeData> notes, trash;
    {
        QSqlQuery query(old_db);
        query.prepare(
                R"(SELECT "id", "creation_date", "modification_date", "content", "full_title" FROM "active_notes")");
        bool status = query.exec();
        if (status) {
            while (query.next()) {
                NodeData node;
                int id = query.value(0).toInt();
                qint64 epochDateTimeCreation = query.value(1).toLongLong();
                QDateTime dateTimeCreation = QDateTime::fromMSecsSinceEpoch(epochDateTimeCreation);
                qint64 epochDateTimeModification = query.value(2).toLongLong();
                QDateTime dateTimeModification =
                        QDateTime::fromMSecsSinceEpoch(epochDateTimeModification);
                QString content = query.value(3).toString();
                QString fullTitle = query.value(4).toString();

                node.setId(id);
                node.setCreationDateTime(dateTimeCreation);
                node.setLastModificationDateTime(dateTimeModification);
                node.setContent(content);
                node.setFullTitle(fullTitle);
                notes.append(node);
            }
        } else {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
        query.clear();
        query.prepare(
                R"(SELECT "id", "creation_date", "modification_date", "deletion_date", "content", "full_title" FROM "deleted_notes")");
        status = query.exec();
        if (status) {
            while (query.next()) {
                NodeData node;
                int id = query.value(0).toInt();
                qint64 epochDateTimeCreation = query.value(1).toLongLong();
                QDateTime dateTimeCreation = QDateTime::fromMSecsSinceEpoch(epochDateTimeCreation);
                qint64 epochDateTimeModification = query.value(2).toLongLong();
                QDateTime dateTimeModification =
                        QDateTime::fromMSecsSinceEpoch(epochDateTimeModification);
                qint64 epochDateTimeDeletion = query.value(3).toLongLong();
                QDateTime dateTimeDeletion = QDateTime::fromMSecsSinceEpoch(epochDateTimeDeletion);
                QString content = query.value(4).toString();
                QString fullTitle = query.value(5).toString();

                node.setId(id);
                node.setCreationDateTime(dateTimeCreation);
                node.setLastModificationDateTime(dateTimeModification);
                node.setDeletionDateTime(dateTimeDeletion);
                node.setContent(content);
                node.setFullTitle(fullTitle);
                trash.append(node);
            }
        } else {
            qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
        }
    }
    auto defaultNoteFolder = getNode(SpecialNodeID::DefaultNotesFolder);
    auto trashFolder = getNode(SpecialNodeID::TrashFolder);
    int nodeId = nextAvailableNodeId();
    int notePos = nextAvailablePosition(defaultNoteFolder.id(), NodeData::Note);
    QString parentAbsPath = defaultNoteFolder.absolutePath();
    m_db.transaction();
    for (auto &note : notes) {
        note.setId(nodeId);
        note.setRelativePosition(notePos);
        note.setAbsolutePath(parentAbsPath + PATH_SEPARATOR + QString::number(nodeId));
        note.setNodeType(NodeData::Note);
        note.setParentId(defaultNoteFolder.id());
        note.setParentName("Notes");
        note.setIsTempNote(false);
        addNodePreComputed(note);
        ++nodeId;
        ++notePos;
    }
    notePos = nextAvailablePosition(trashFolder.id(), NodeData::Note);
    parentAbsPath = trashFolder.absolutePath();
    for (auto &note : trash) {
        note.setId(nodeId);
        note.setRelativePosition(notePos);
        note.setAbsolutePath(parentAbsPath + PATH_SEPARATOR + QString::number(nodeId));
        note.setNodeType(NodeData::Note);
        note.setParentId(trashFolder.id());
        note.setParentName("Trash");
        note.setIsTempNote(false);
        addNodePreComputed(note);
        ++nodeId;
        ++notePos;
    }
    QSqlQuery query(m_db);
    query.prepare(R"(UPDATE "metadata" SET "value"=:value WHERE "key"='next_node_id';)");
    query.bindValue(":value", nodeId + 1);
    if (!query.exec()) {
        qDebug() << __FUNCTION__ << __LINE__ << query.lastError();
    }
    m_db.commit();
    {
        old_db.close();
        old_db = QSqlDatabase::database();
    }
    QSqlDatabase::removeDatabase(OUTSIDE_DATABASE_NAME);
    recalculateChildNotesCount();
}

void DBManager::onChangeDatabasePathRequested(const QString &newPath)
{
    {
        m_db.commit();
        m_db.close();
        m_db = QSqlDatabase::database();
    }
    QFile::rename(m_dbpath, newPath);
    open(newPath, false);
}

bool DBManager::IsDatabaseHasNotes()
{
    QSqlQuery checkQuery(m_db);
    checkQuery.prepare("SELECT COUNT(*) FROM node_table WHERE node_type = :note_type;");
    checkQuery.bindValue(":note_type", static_cast<int>(NodeData::Note));
    checkQuery.exec();
    checkQuery.next();

    return checkQuery.value(0).toInt() > 0;
}

void DBManager::addExampleNotes()
{
    QStringList exampleNotes = {
        "# Renaissance (example note)\n\n^The Renaissance is a period in history and a cultural "
        "movement marking the transition from the Middle Ages to modernity, covering the 15th and "
        "16th centuries and characterized by an effort to revive and surpass the ideas and achievements "
        "of classical antiquity.\n\nIt was associated with great social change in most fields and disciplines, "
        "including art, architecture, politics, literature, exploration and science. It began in the Republic of "
        "Florence, then spread to the rest of Italy and later throughout Europe. The term rinascita (\"rebirth\") "
        "first appeared in Lives of the Artists (c. 1550) by Giorgio Vasari, while the corresponding French word "
        "renaissance was adopted into English as the term for this period during the 1830s.\n\n**Source**: [Wikipedia]"
        "(https://en.wikipedia.org/wiki/Renaissance)\n\n{{image \"caption\":\"The School of Athens by Raphael (source: "
        "Wikipedia)\",\"source\":\"qrc:/images/The_School_of_Athens.jpeg\",\"width\":900}}\n{{/image}}\n\n## The School of "
        "Athens\n\n^The School of Athens (Italian: Scuola di Atene) is a fresco by the Italian Renaissance artist Raphael. "
        "It was painted between 1509 and 1511 as part of a commission by Pope Julius II to decorate the rooms now called the "
        "Stanze di Raffaello in the Apostolic Palace in Vatican City.\n\nThe fresco depicts a congregation of ancient philosophers"
        ", mathematicians, and scientists, with Plato and Aristotle featured in the center. The identities of most figures are "
        "ambiguous or discernable only through subtle details or allusions among those commonly identified are Socrates, "
        "Pythagoras, Archimedes, Heraclitus, Averroes, and Zarathustra.\n\n**Source**: [Wikipedia](https://en.wikipedia.org"
        "/wiki/The_School_of_Athens)\n\n{{image \"caption\":\"Lady with an Ermine by Leonardo da Vinci (source: "
        "Wikipedia)\",\"source\":\"qrc:/images/Lady_with_an_Ermine.jpeg\",\"width\":557}}\n{{/image}}\n\n## Lady "
        "with an Ermine\n\n^The Lady with an Ermine is a portrait painting widely attributed to the Italian Renaissance "
        "artist Leonardo da Vinci. Dated to c. 1489–1491, the work is painted in oils on a panel of walnut wood. Its subject "
        "is Cecilia Gallerani, a mistress of Ludovico Sforza (\"Il Moro\"), Duke of Milan; Leonardo was painter to the Sforza"
        " court in Milan at the time of its execution.\n\nIt is the second of only four surviving portraits of women painted by"
        "Leonardo, the others being Ginevra de' Benci, La Belle Ferronnière and the Mona Lisa. Lady with an Ermine is now housed"
        "at the Czartoryski Museum in Kraków, and is one of Poland's national treasures.\n\n**Source**: [Wikipedia]"
        "(https://en.wikipedia.org/wiki/Lady_with_an_Ermine)"
        ,
        "# This is a line (example note)\n\n^This is a line.<br />It can be part of a *poem*. Or a **story**.<br />"
        "It can be a thought of you,<br />Something to ~remember~.\n\n- It can be an idea.\n\t- Or a conversation,\n\t\t- With "
        "someone else or yourself.\n\t\t\t- It can be part of your diary,\n\t\t\t\t- Or your goals.\n\n1. It can be a movie\n2. or "
        "a book.\n\n- [ ] A to-do list,\n\t- [x] To further get to your goals.\n\n> It can be an advice, <br />a tip <br />or a quote"
        ".\n\nThis is a line.\n**It can be anything that matters to you.**"
        ,
        "# Project Plan (example note)\n\n- Do people need my idea in their life?\n\t- Technical feasability\n\t\t- Study new tech "
        "stack\n\t\t\t- \t\t\t\t- Investors\n\t\t\t\t- Marketing\n\n{{kanban \"contentX\":-50}}\n# First Steps\n\n- [x] ### Idea Test\n"
        "Ask people about their routine to see if there's a need for our product\n- [x] ### Mockup\nCreate a sketch mockup for our idea"
        "\n1. Draft on paper first\n2. Move sketch to Keynote\n- [x] ### Technical feasibility\nAsses what's the fastest and easiest way"
        " I can build this based **on my own skills**.\n\n# MVP\n- [ ] ### Tech bloggers\nCold email tech bloggers that might be interested"
        " in our product\n\n- [ ] ### Minmal version\nWrite processing code in C++\n____\nFigure how to connect signals from C++ to QML\n- [ ]"
        " ### UI\nComplete UI based on mockups\n- [ ] ### Backend and UI\nCombine the C++ backend with the QML UI\n\n# Publish\n\n- [ ] ### "
        "Investors\nSpeak with investors?\n- [ ] ### Ads\nResearch if ads will be cost effective for us\n\n- [ ] ### Study QML\n- Buy course\n"
        "- Create small projects on the way\n# Defer For Later\n\n- [ ] A/B test home page\n- [ ] Internationalization (Translate the website "
        "and support other languages)\n- [ ] Code optimizations\n- [ ] Some cool feature that isn't part of the MVP...\n{{/kanban}}"
        ,
        "# ⭐ Today (example note)\n\n- [x] Buy milk\n- [ ] Go watch a movie with Charlie\n\n**Project**\n---\n- [x] To do 1\n"
        "- [x] To do 2\n- [x] To do 3\n\n**Another Project**\n---\n- [ ] To do 1\n- [ ] To do 2\n- [ ] To do 3"
        ,
        "# 📅 Weekly Goals (example note)\n\n{{kanban \"contentX\":-12}}\n## Monday\n\n- [x] Work on project\n- [ ] Exercise\n"
        "- [ ] Read a book\n\n## Tuesday\n\n- [ ] Prepare for meeting\n- [ ] Go to the gym\n- [ ] Write report\n\n## Wednesday\n\n"
        "- [ ] Finish project\n- [ ] Go for a run\n- [ ] Relax and watch a movie\n\n## Thursday\n\n- [ ] Study for exam\n- [ ] Meet "
        "with friends\n- [ ] Plan weekend trip\n\n## Friday\n\n- [ ] Take exam\n- [ ] Celebrate with friends\n- [ ] Pack for trip\n\n"
        "## Saturday\n\n- [ ] Go on weekend trip\n- [ ] Explore new places\n- [ ] Relax and enjoy\n\n## Sunday\n\n- [ ] Return from trip"
        "\n- [ ] Unpack and relax\n- [ ] Plan for next week\n{{/kanban}}\n> *\"We are what we repeatedly do. Excellence, then, is not an "
        "act, but a habit.\"*\n\n— Will Durant, summarizing Aristotle's ethics in *The Story of Philosophy*"
    };

    addNotesToDefaultFolder(exampleNotes);
}

void DBManager::addNotesToDefaultFolder(const QStringList &notes)
{
    auto defaultNoteFolder = getNode(SpecialNodeID::DefaultNotesFolder);
    int nodeId = nextAvailableNodeId();
    int notePos = nextAvailablePosition(defaultNoteFolder.id(), NodeData::Note);
    QString parentAbsPath = defaultNoteFolder.absolutePath();
    QDateTime currentDate = QDateTime::currentDateTime();

    for (auto &noteString : notes) {
        NodeData note;
        note.setFullTitle(noteString.split("\n", Qt::SkipEmptyParts).first());
        note.setContent(noteString);
        note.setId(nodeId);
        note.setRelativePosition(notePos);
        note.setAbsolutePath(parentAbsPath + PATH_SEPARATOR + QString::number(nodeId));
        note.setNodeType(NodeData::Note);
        note.setParentId(defaultNoteFolder.id());
        note.setParentName("Notes");
        note.setIsTempNote(false);
        note.setCreationDateTime(currentDate);
        note.setLastModificationDateTime(currentDate);

        addNode(note);
        ++nodeId;
        ++notePos;
    }

    recalculateChildNotesCount();
    onNodeTagTreeRequested();
}

void DBManager::addNotesToNewImportedFolder(const QList<QPair<QString, QDateTime>> &fileDatas)
{
    NodeData newFolder;
    newFolder.setNodeType(NodeData::Folder);
    QDateTime currentDate = QDateTime::currentDateTime();
    newFolder.setCreationDateTime(currentDate);
    newFolder.setLastModificationDateTime(currentDate);
    newFolder.setFullTitle("Imported Notes");
    newFolder.setParentId(SpecialNodeID::RootFolder);

    int newFolderId = addNode(newFolder);
    if (newFolderId <= 0) {
        qDebug() << "Failed to add 'Imported Notes' folder.";
        return;
    }

    NodeData createdFolder = getNode(newFolderId);
    QString parentAbsPath = createdFolder.absolutePath();

    int nodeId = nextAvailableNodeId();
    int notePos = 0;

    for (const auto &noteData : fileDatas) {
        NodeData note;
        note.setFullTitle(noteData.first.section('\n', 0, 0, QString::SectionSkipEmpty));
        note.setContent(noteData.first);
        note.setId(nodeId++);
        note.setRelativePosition(notePos++);
        note.setAbsolutePath(parentAbsPath + PATH_SEPARATOR + QString::number(note.id()));
        note.setNodeType(NodeData::Note);
        note.setParentId(newFolderId);
        note.setCreationDateTime(noteData.second);
        note.setLastModificationDateTime(noteData.second);

        addNode(note);
    }

    recalculateChildNotesCount();
    onNodeTagTreeRequested();
}

void DBManager::exportNotes(const QString &baseExportPath, const QString &extension)
{
    // Ensure the export directory exists
    QString rootFolderName = QStringLiteral("Plume Notes");
    QString exportPathNew = baseExportPath + QDir::separator() + rootFolderName;
    QDir directory;

    int counter = 1;
    while (directory.exists(exportPathNew)) {
        exportPathNew = baseExportPath + QDir::separator() + rootFolderName + " " + QString::number(counter++);
    }
    qDebug() << "Exporting notes to:" << exportPathNew;
    directory.mkpath(exportPathNew);

           // Retrieve all folders first to create the directory structure
    QVector<NodeData> folders = getAllFolders();

           // Create directories for each folder
    QMap<int, QString> folderPaths;
    for (const NodeData &folder : folders) {
        QString path = exportPathNew;
        if (folder.id() != SpecialNodeID::RootFolder) { // Skip root folder
            QStringList folderNames;
            QString currentPath = folder.absolutePath();
            QStringList pathParts = currentPath.split(QDir::separator(), Qt::SkipEmptyParts);
            for (const auto &part : pathParts) {
                int id = part.toInt();
                if (id == SpecialNodeID::RootFolder)
                    continue;
                auto folderTemp = getNode(id);
                folderNames << folderTemp.fullTitle();
            }
            QString relativePath = folderNames.join(QDir::separator());
            path += QDir::separator() + relativePath;

            counter = 1;
            while (directory.exists(path)) {
                path = exportPathNew + QDir::separator() + relativePath + " " + QString::number(counter++);
            }

            QDir().mkpath(path);
        }
        // qDebug() << "path: " << path;
        folderPaths.insert(folder.id(), path);
    }

           // Retrieve all notes
    QSqlQuery query(m_db);
    query.prepare(R"(SELECT "id", "title", "content", "parent_id" FROM node_table WHERE node_type = :note_type)");
    query.bindValue(":note_type", static_cast<int>(NodeData::Type::Note));

    if (!query.exec()) {
        qDebug() << "Failed to retrieve notes for export:" << query.lastError();
        return;
    }

           // Export each note as a .txt file in its corresponding directory
    QTextDocument doc;
    while (query.next()) {
        // int noteId = query.value(0).toInt();
        QString title = query.value(1).toString();
        QString content = query.value(2).toString();
        int parentId = query.value(3).toInt();

        QString notePath = folderPaths[parentId];
        QString safeTitle = title;
        if (safeTitle.contains("<br />"))
            safeTitle = safeTitle.section("<br />", 0, 0, QString::SectionSkipEmpty);
        safeTitle = safeTitle.simplified();
        doc.setMarkdown(safeTitle);
        safeTitle = doc.toPlainText();
        safeTitle.replace(QRegularExpression(R"([\/\\:*?"<>|])"), "_"); // Make the title filesystem-safe
        QString filePath = notePath + QDir::separator() + safeTitle + extension;

        if (safeTitle.isEmpty()) {
            safeTitle = QStringLiteral("Untitled Note");
        }

        counter = 1;
        while (directory.exists(filePath)) {
            filePath = notePath + QDir::separator() + safeTitle + " " + QString::number(counter++) + extension;
        }

        // qDebug() << "Exporting note:" << filePath;

        QFile file(filePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << content;
            file.close();
        } else {
            qDebug() << "Failed to export note:" << filePath;
        }
    }

    qDebug() << "Export completed to:" << baseExportPath;
}
