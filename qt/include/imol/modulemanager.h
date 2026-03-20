#ifndef IMOL_MODULEMANAGER_H
#define IMOL_MODULEMANAGER_H

#include "core_global.h"

#include <QObject>
#include <QHash>
#include <QVariant>

#ifndef IMOL_MODULE_NAME_SEPARATOR
#define IMOL_MODULE_NAME_SEPARATOR "."
#endif

#ifndef IMOL_MODULE_SPECIAL_START
#define IMOL_MODULE_SPECIAL_START "_"
#endif

#ifndef IMOL_MODULE_ID_START
#define IMOL_MODULE_ID_START "#"
#endif

#ifndef IMOL_ROOT_MODULE_NAME
#define IMOL_ROOT_MODULE_NAME "@m"
#endif

#ifndef IMOL_EMPTY_MODULE_NAME
#define IMOL_EMPTY_MODULE_NAME "@e"
#endif

#ifndef IMOL_MODULE_XML_INDENT
#define IMOL_MODULE_XML_INDENT 4
#endif

class QMutex;
class QXmlStreamReader;
class QXmlStreamWriter;

namespace imol {

//!
//! \brief The ModuleObject (\short mobj) class saves a variant and controls the signals of change add remove operations,
//! working as one node in an data tree, a ModuleObject provides a series of methods to build and search its children.
//!
//! \brief The relative name (\short rname) gives two way of naming a mobj, absolute name or \def ID_START plus its index
//! \brief The relative path (\short rpath) describes a search path of mobj tree branch, combinds all rnames with \def NAME_SEPARATOR
//!
//! \author Thilo
//!
class CORESHARED_EXPORT ModuleObject : public QObject
{
    Q_OBJECT

public:
    enum ActivateType {
        MOBJ_CHANGE,
        MOBJ_INSERT,
        MOBJ_REMOVE,
        MOBJ_REORDER
    };

public:
    explicit ModuleObject(const QString &name = "", ModuleObject *pmobj = nullptr);
    virtual ~ModuleObject();

    //!
    //! ---- self properties ----
    //!

    virtual bool isEmptyMobj() const;
    bool isNull() const { return isEmptyMobj(); }

    //! \property name is the basic property for ModuleObject
    Q_PROPERTY(QString m_name READ name CONSTANT)
    QString name() const;

    //! \property quiet restricts all signals and activation
    Q_PROPERTY(bool m_is_quiet READ isQuiet WRITE quiet CONSTANT)
    bool isQuiet() const;
    void quiet(bool is_quiet, bool is_recursively = false);

    //! \property watch controls wether parent nodes are able to be activated
    Q_PROPERTY(bool m_is_watching READ isWatching WRITE watch CONSTANT)
    bool isWatching() const;
    void watch(bool is_watching, bool is_recursively = false);

    //!
    //! ---- static utilities ----
    //!

    //! the integer index of a rname string
    static int indexOfRname(const QString &rname);

    //! creates the real name of a rname as an available cmobj for given mobj
    static QString generateId(ModuleObject *mobj);

    //!
    //! ---- parent module object (\short pmobj) ----
    //!
    //! \param level is pmobj iteration number

    ModuleObject * pmobj() const;
    ModuleObject * pmobj(int level) const;
    ModuleObject * p(int level = 0) const; // shortcut

    ModuleObject * parentData(int level = 0) const { return pmobj(level); }

    //!
    //! ---- brother module object (\short bmobj) ----
    //!
    //! \param next is next bmobj iteration number when positive, or previous bmobj if negative

    ModuleObject * prevBmobj() const;
    ModuleObject * nextBmobj() const;
    ModuleObject * bmobj(int next) const;
    ModuleObject * b(int next = 1) const; // shortcut
    ModuleObject * b(const QString &rname, bool is_strict = false) const;

    ModuleObject * prev() const { return prevBmobj(); }
    ModuleObject * next() const { return nextBmobj(); }
    ModuleObject * brotherData(int next) const { return b(next); }
    ModuleObject * brotherData(const QString &rname, bool is_strict = false) const { return b(rname, is_strict); }

    int indexInPmobj() const;

    int indexInParentData() const { return indexInPmobj(); }

    //! check whether name indicates an index
    bool isRelative() const;

    //! rname is relative name of this mobj
    QString rname() const;

    //!
    //! ---- child module object (cmobj) ----
    //!
    //! \param index is normally index of rname, counting form last is possible by means of negative index

    ModuleObject * cmobj(const QString &rname, bool is_strict = false) const;
    ModuleObject * c(const QString &rname, bool is_strict = false) const; // shortcut
    ModuleObject * cmobj(int index) const;
    ModuleObject * c(int index) const; // shortcut

    ModuleObject * childData(const QString &rname, bool is_strict = false) const { return c(rname, is_strict); }
    ModuleObject * childData(int index) const { return c(index); }

    bool hasCmobj(const QString &rname = "", bool is_strict = false) const;
    bool hasCmobj(ModuleObject *mobj) const;

    bool hasChildData(const QString &rname = "", bool is_strict = false) const { return hasCmobj(rname, is_strict); }
    bool hasChildData(ModuleObject* child_d) const { return hasCmobj(child_d); }

    ModuleObject * first() const;
    ModuleObject * last() const;

    int cmobjCount() const;
    int size() const;

    int childrenDataCount() const { return cmobjCount(); }

    QStringList cmobjNames(bool is_strict = false) const;
    QList<ModuleObject *> cmobjs(bool is_ordered = false) const; // is_ordered is obsolete

    QStringList childrenDataNames(bool is_strict = false) const { return cmobjNames(is_strict); }
    QList<ModuleObject *> childrenData() const { return cmobjs(); }

    //!
    //! ---- relative module object (rmobj) ----
    //!

    ModuleObject * rmobj(const QString &rpath) const;
    ModuleObject * r(const QString &rpath) const; // shortcut

    ModuleObject * relativeData(const QString &rpath) const { return rmobj(rpath); }

    bool hasRmobj(const QString &rpath) const;
    bool isRmobjOf(ModuleObject *mobj) const;

    bool hasRelativeData(const QString &rpath) const { return hasRmobj(rpath); }
    bool isRelativeOf(ModuleObject* ancestor_d) const { return isRmobjOf(ancestor_d); }

    //! fullName is the rpath from given mobj, \default nullptr stands for global root mobj
    QString fullName(ModuleObject *ancestor_d = nullptr) const;

    //!
    //! ---- data control ----
    //!

    void activate(QObject *context, ModuleObject *changed_mobj, ActivateType type);

    virtual const QVariant& get(const QVariant &default_var = QVariant()) const;

    bool getBool(bool default_bool = false) const;
    int getInt(int default_int = 0) const;
    double getDouble(double default_double = 0) const;
    QString getString(const QString &default_str = "") const;

    QVariantList getList() const;

    virtual ModuleObject * set(QObject *context, const QVariant &var);
    virtual ModuleObject * set(QObject *context, const QString &rpath, const QVariant &var, bool intelligent = true);

    ModuleObject * set(const QVariant &var) { return set(this, var); }
    ModuleObject * set(const QString &rpath, const QVariant &var) { return set(this, rpath, var); }

    void setList(QObject *context, const QVariantList &vars, bool intelligent = true);
    void setList(QObject *context, const QString &rpath, const QVariantList &vars, bool intelligent = true);

    virtual bool insert(QObject *context, ModuleObject *mobj, bool auto_replace = false, int index = -1);
    virtual bool insert(QObject *context, const QString &rpath, bool auto_replace = false);
    virtual ModuleObject * append(QObject *context, const QString &name = "");

    virtual bool remove(QObject *context, ModuleObject *mobj, bool auto_delete = true);
    virtual bool remove(QObject *context, const QString &rpath, bool auto_delete = true);

    virtual void copyFrom(QObject *context, ModuleObject *other, bool auto_insert = true, bool auto_remove = false);

    bool reorder(QObject *context, int from_index, int to_index);
    bool reorder(QObject *context, const QStringList &rnames);

    void clear(QObject *context, bool auto_delete = true, bool only_var = false);

    //! output all info into log
    void dump(ModuleObject *rmobj = nullptr) const;

    void importFromVariant(QObject *context, const QVariant &var, bool auto_insert = true, bool auto_replace = true, bool auto_remove = false);
    QVariant exportToVariant(bool auto_ignore = true) const;

    //!
    //! \brief use json struction to construct children nodes
    //!
    //! Json Value (\p value) -> set current mobj with \p value
    //! Json Object (\p key, \p value) -> create cmobj with name is \p key, its structure is defined with \p value
    //! Json Array (\p value) -> Module Object with name started with IMOL_MODULE_ID_START, its structure id defined with \p value
    //!
    virtual void importFromJson(QObject *context, const QJsonValue &json_value, bool auto_insert = true, bool auto_replace = true, bool auto_remove = false);
    virtual QJsonValue exportToJson(bool auto_ignore = true) const;

    //!
    //! \brief use xml struction to construct children nodes, saves current var value only when can convert to QString
    //!
    virtual void importFromXml(QObject *context, QXmlStreamReader &xml_reader, bool auto_insert = true, bool auto_replace = true, bool only_standard = true);
    virtual void exportToXml(QXmlStreamWriter &xml_writer, bool is_root = true, bool auto_ignore = true) const;

    void importFromXmlStr(QObject *context, const QString &xml_str, bool auto_insert = true, bool auto_replace = true, bool only_standard = true);
    QString exportToXmlStr(int indent = IMOL_MODULE_XML_INDENT, bool auto_ignore = true) const;

    //!
    //! \brief use custom binary to construct children nodes, saves current var value with all support types. Custom variant will
    //! be copied directly, unsupported types are banned and covered with empty QVariant
    //!
    //! \caption Unsupport types
    //! \enum UnknownType, Void, Nullptr, VoidStar, QObjectStar
    //!
    //! \warning use invalid type of variant (like pointer) may CRASH
    //!
    virtual void importFromBin(QObject *context, const QByteArray &bytes, bool auto_insert = true, bool auto_replace = true, bool auto_remove = false);
    virtual QByteArray exportToBin(bool auto_ignore = true) const;

signals:
    /**
     * @brief activated signal is emitted when child mobj changed
     * @param changed_mobj is the changed mobj
     * @param changer is the context
     */
    void activated(imol::ModuleObject *changed_mobj, int type, QObject *changer);
    /**
     * @brief changed signal is emitted when value changed
     * @param var is the new value
     * @param changer is the context
     */
    void changed(const QVariant &var, const QVariant &old_var, QObject *changer);
    /**
     * @brief added signal is emitted when this mobj inserts new child
     * @param rname is the child rname
     * @param changer is the context
     */
    void added(const QString &rname, QObject *changer);
    /**
     * @brief removed signal is emmited when this mobj removes its child
     * @param rname is the child rname
     * @param changer is the context
     */
    void removed(const QString &rname, QObject *changer);

    /**
     * @brief gonnaInsert signal is emmited when this mobj will add a new child
     * @param target is the inserting child
     * @param ref is this mobj
     * @param changer is the context
     */
    void gonnaInsert(imol::ModuleObject *target, imol::ModuleObject *ref, QObject *changer);
    /**
     * @brief gonnaRemove signal is emmited when this mobj will remove an exsisting child
     * @param target is the removing child
     * @param ref is this mobj
     * @param changer is the context
     */
    void gonnaRemove(imol::ModuleObject *target, imol::ModuleObject *ref, QObject *changer);

public slots:
    /**
     * @brief trigger slot works as a changed signal generator
     */
    void trigger();

protected:
    void setName(const QString &name);
    void setPmobj(ModuleObject *mobj);
    void setPrevBmobj(ModuleObject *mobj);
    void setNextBmobj(ModuleObject *mobj);

private:
    // parent
    ModuleObject *m_pmobj;

    // brother
    ModuleObject *m_prev_bmobj;
    ModuleObject *m_next_bmobj;

    // children
    ModuleObject *m_first_cmobj;
    ModuleObject *m_last_cmobj;
    QHash<QString, ModuleObject *> m_cmobjs;

    // self
    QString m_name;
    QVariant m_var;

    bool m_is_quiet;
    bool m_is_watching;

    QMutex *m_mutex;
};

//!
//! \brief The EmptyModuleObject class is a mobj which does nothing, helpful for avoiding crash.
//!
class CORESHARED_EXPORT EmptyModuleObject : public ModuleObject
{
    Q_OBJECT

public:
    explicit EmptyModuleObject(const QString &name, ModuleObject *parent_mobj = nullptr);

    bool isEmptyMobj() const override;

    const QVariant& get(const QVariant &default_var = QVariant()) const override;

    ModuleObject * set(QObject *context, const QVariant &var) override;
    ModuleObject * set(QObject *context, const QString &rpath, const QVariant &var, bool intelligent = true) override;

    void copyFrom(QObject *context, ModuleObject *other, bool auto_insert = true, bool auto_remove = false) override;

    bool insert(QObject *context, ModuleObject *mobj, bool auto_replace = false, int index = -1) override;
    bool insert(QObject *context, const QString &rpath, bool auto_replace = false) override;
    ModuleObject * append(QObject *context, const QString &rname = "") override;

    bool remove(QObject *context, ModuleObject *mobj, bool auto_delete = true) override;
    bool remove(QObject *context, const QString &rpath, bool auto_delete = true) override;

    void importFromJson(QObject *context, const QJsonValue &json_value, bool auto_insert = true, bool auto_replace = true, bool auto_remove = false) override;
    QJsonValue exportToJson(bool auto_ignore = true) const override;

    void importFromXml(QObject *context, QXmlStreamReader &xml_reader, bool auto_insert = true, bool auto_replace = true, bool only_standard = true) override;
    void exportToXml(QXmlStreamWriter &xml_writer, bool is_root = true, bool auto_ignore = true) const override;

    void importFromBin(QObject *context, const QByteArray &bytes, bool auto_insert = true, bool auto_replace = true, bool auto_remove = false) override;
    QByteArray exportToBin(bool auto_ignore = true) const override;
};

//!
//! \brief The ModuleManager class is a global mobj container
//!
class CORESHARED_EXPORT ModuleManager : public QObject
{
    Q_OBJECT

public:
    explicit ModuleManager(QObject *parent = nullptr);
    virtual ~ModuleManager();

    static ModuleManager & instance();
    static QString separator();

    ModuleObject * rootMobj() const;
    ModuleObject * emptyMobj() const;

    ModuleObject * rootData() const { return rootMobj(); }
    ModuleObject * nullData() const { return emptyMobj(); }

    void reserve(int max_size);
    ModuleObject * create(const QString &name, ModuleObject *pmobj = nullptr);

    ModuleObject * regist(QObject *context, const QString &full_name);
    bool cancel(QObject *context, const QString &full_name);

    bool importFromJson(QObject *context, const QString &json_path, const QString &prefix = "", bool is_watched = false, bool auto_insert = true); //obsolete
    bool exportToJson(QObject *context, const QString &full_name, const QString &json_path); //obsolete

    static QString convertToUtf8(const QByteArray &content);

    static QVariant readFromJsonAsVariant(const QString &json_path);

    static QJsonValue readFromJson(const QString &json_path);
    static bool writeToJson(const QString &json_path, const QJsonValue &json_value, bool auto_create_path = true);

    static bool readFromXmlFile(QObject *context, ModuleObject *mobj, const QString &xml_path);
    static bool writeToXmlFile(const QString &xml_path, ModuleObject *mobj, bool auto_create_path = true, bool auto_ignore = true);

    static QByteArray readFromBin(const QString &bin_path);
    static bool writeToBin(const QString &bin_path, const QByteArray &bin_bytes, int compress_level = -1, bool auto_create_path = true);

private:
    ModuleObject *m_root_mobj;
    EmptyModuleObject *m_empty_mobj;
    QList<ModuleObject *> m_backup_mobjs;
};

//output function
CORESHARED_EXPORT imol::ModuleManager & m();
CORESHARED_EXPORT imol::ModuleObject * m(const QString &rpath, imol::ModuleObject *relative_mobj = nullptr);

CORESHARED_EXPORT QString mIndex(int index, const QString &default_name = "");
CORESHARED_EXPORT QString mName(const QString &n0, const QString &n1);
CORESHARED_EXPORT QString mName(const QString &n0, const QString &n1, const QString &n2);
CORESHARED_EXPORT QString mName(const QString &n0, const QString &n1, const QString &n2, const QString &n3);
CORESHARED_EXPORT QString mName(const QString &n0, const QString &n1, const QString &n2, const QString &n3, const QString &n4);
CORESHARED_EXPORT QString mName(const QString &n0, const QString &n1, const QString &n2, const QString &n3, const QString &n4, const QString &n5);
CORESHARED_EXPORT QString mName(const QString &n0, const QString &n1, const QString &n2, const QString &n3, const QString &n4, const QString &n5, const QString &n6);

}

#endif // IMOL_MODULEMANAGER_H
