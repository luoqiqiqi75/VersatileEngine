#include "ve/qt/qt_entry.h"
#include "ve/qt/qt_loop.h"

#include "ve/entry.h"
#include "ve/core/log.h"
#include "ve/core/module.h"
#include "ve/core/node.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QIODevice>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QThread>
#include <QTimer>
#include <QTimerEvent>
#include <QWidget>
#include <QtGlobal>

#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <unordered_map>

#ifdef VE_QT_HAS_IMOL
#  include "imol/logmanager.h"
#  include "ve/qt/imol_legacy.h"
#endif

namespace ve::qt {

namespace {

Node* qtConfigNode()
{
    Node* r = node::root();
    if (!r) {
        return nullptr;
    }
    return r->find("ve/qt/config");
}

class QtOperationRecorder : public QObject
{
public:
    explicit QtOperationRecorder(QObject* parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        static bool event_lock = false;
        auto lock_f = [] {
            if (event_lock) {
                return false;
            }
            event_lock = true;
            QTimer::singleShot(0, [] { event_lock = false; });
            return true;
        };
        switch (e->type()) {
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick:
                if (qobject_cast<QWidget*>(o) && lock_f()) {
                    if (auto ab = qobject_cast<QAbstractButton*>(o)) {
                        if (ab->text().isEmpty()) {
                            qInfo() << "<rescue.record> [BTN]" << dynamic_cast<QMouseEvent*>(e) << o;
                        } else {
                            qInfo().nospace() << "<rescue.record> [BTN " << ab->text() << "] "
                                              << dynamic_cast<QMouseEvent*>(e) << " " << o;
                        }
                    } else {
                        qInfo() << "<rescue.record>" << dynamic_cast<QMouseEvent*>(e) << o;
                    }
                }
                break;
            case QEvent::KeyPress:
                if (qobject_cast<QWidget*>(o) && lock_f()) {
                    qInfo() << "<rescue.record>" << dynamic_cast<QKeyEvent*>(e) << o;
                }
                break;
            default: break;
        }
        return false;
    }
};

static void installQtMessageHandlerIfNeeded(Node* cfg)
{
    bool on = true;
    if (cfg) {
        if (Node* n = cfg->find("log/install_qt_message_handler")) {
            on = n->getBool(true);
        }
    }
    if (!on) {
        return;
    }

#if defined(Q_OS_ANDROID) && defined(VE_QT_HAS_IMOL)
    static QtMessageHandler default_handler = nullptr;
    default_handler = qInstallMessageHandler(
        [](QtMsgType type, const QMessageLogContext& context, const QString& msg) {
            switch (type) {
                case QtDebugMsg:
                    imol::FLog(LOG_LEVEL_STR_DEBUG, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg;
                    break;
                case QtInfoMsg:
                    imol::FLog(LOG_LEVEL_STR_INFO, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg;
                    break;
                case QtWarningMsg:
                    imol::FLog(LOG_LEVEL_STR_WARNING, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg;
                    break;
                case QtCriticalMsg:
                    imol::FLog(LOG_LEVEL_STR_ERROR, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg;
                    break;
                case QtFatalMsg:
                    imol::FLog(LOG_LEVEL_STR_SUDO, "", LOG_FILE_NAME_PREFIX, LOG_FILE_NAME_SUFFIX) << msg;
                    break;
            }
            if (default_handler) {
                default_handler(type, context, msg);
            }
        });
#else
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& msg) {
        QString thread_info = (QThread::currentThread() == QCoreApplication::instance()->thread())
            ? QStringLiteral("M")
            : QString::number(reinterpret_cast<unsigned long long>(QThread::currentThreadId()) & 0xffffffff, 16);
        switch (type) {
            case QtDebugMsg:
                veLogD << thread_info.toStdString() << ") " << msg.toLocal8Bit().constData();
                break;
            case QtInfoMsg:
                veLogI << thread_info.toStdString() << ") " << msg.toStdString();
                break;
            case QtWarningMsg:
                veLogW << thread_info.toStdString() << ") " << msg.toStdString();
                break;
            case QtCriticalMsg:
                veLogE << thread_info.toStdString() << ") " << msg.toStdString();
                break;
            case QtFatalMsg:
                veLogS << thread_info.toStdString() << ") " << msg.toStdString();
                break;
        }
    });
#endif
}

} // namespace

void applyEarlySettings()
{
    Node* cfg = qtConfigNode();

    bool share_gl = true;
    bool use_96 = QCoreApplication::testAttribute(Qt::AA_Use96Dpi);
    if (cfg) {
        if (Node* n = cfg->find("attr/ShareOpenGLContexts")) {
            share_gl = n->getBool(true);
        }
        if (Node* n = cfg->find("attr/Use96Dpi")) {
            use_96 = n->getBool(QCoreApplication::testAttribute(Qt::AA_Use96Dpi));
        }
    }
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts, share_gl);
    QCoreApplication::setAttribute(Qt::AA_Use96Dpi, use_96);

    if (!cfg) {
        return;
    }

    if (Node* env = cfg->find("env")) {
        for (Node* ch : *env) {
            const std::string& k = ch->name();
            if (k.empty()) {
                continue;
            }
            std::string v = ch->getString("");
            qputenv(k.c_str(), QByteArray(v.data(), int(v.size())));
        }
    }

    if (Node* prep_root = cfg->find("path/prepend")) {
#if defined(Q_OS_WIN)
        const char* env_key = "PATH";
        const char sep = ';';
#else
        const char* env_key = "LD_LIBRARY_PATH";
        const char sep = ':';
#endif
        QString full = QString::fromLocal8Bit(qgetenv(env_key));
        QString current_dir = QDir().absolutePath();
        for (Node* path_n : *prep_root) {
            QString env_path = QString::fromStdString(path_n->getString(""));
            if (env_path.startsWith('.')) {
                env_path.replace(0, 1, current_dir);
            }
            env_path.replace('/', QDir::separator());
            if (!full.isEmpty()) {
                full.append(sep);
            }
            full.append(env_path);
        }
        qputenv(env_key, full.toLocal8Bit());
    }
}

// ============================================================================
// QtTimers — QObject timerEvent scheduler shared by QtLoop and QtMainLoop
// ============================================================================
//
// Qt's native timer primitive is QObject::startTimer + timerEvent; QTimer is a
// QObject wrapper on top of it. Since ve::Object exposes the same shape, this
// binds straight to the primitive: one host QObject carries every timer on the
// loop, instead of allocating a QObject each.
//
// Qt timers may only be started and stopped on the thread that owns the host,
// but addTimer()/removeTimer() may be called from any thread, so both marshal
// onto the host. Each record carries a `cancelled` flag that takes effect
// synchronously on the calling thread. It prevents any later dispatch even
// though the killTimer() behind it is queued, and lets a remove that beats the
// queued arm win; a tick already executing is not preempted.
//
// Queued lambdas capture the host and the record, never the QtTimers — a
// QObject's pending posted events die with it, so teardown cannot leave a
// lambda pointing at freed scheduler state.
//
namespace {

struct QtTimerRec
{
    Task tick;
    Loop::TimerHandle handle = 0;
    int  interval = 0;
    bool repeat = true;
    std::atomic<bool> cancelled{false};
    int  qt_id = 0;   // host thread only

    bool dead() const { return cancelled.load(std::memory_order_acquire); }
    void cancel() { cancelled.store(true, std::memory_order_release); }
};

using QtTimerRecPtr = std::shared_ptr<QtTimerRec>;

// The registry is shared independently of QtTimers so the host can retire a
// single-shot before its tick without keeping a back-pointer to the scheduler.
// QtTimers may disappear while host work is still queued; the host therefore
// keeps only a weak reference to this state.
struct QtTimerState
{
    using TimerHandle = Loop::TimerHandle;

    void add(const QtTimerRecPtr& rec)
    {
        std::lock_guard<std::mutex> lk(mtx);
        recs[rec->handle] = rec;
    }

    QtTimerRecPtr take(TimerHandle handle)
    {
        std::lock_guard<std::mutex> lk(mtx);
        auto it = recs.find(handle);
        if (it == recs.end()) return {};
        QtTimerRecPtr rec = std::move(it->second);
        recs.erase(it);
        return rec;
    }

    void retire(const QtTimerRecPtr& rec)
    {
        std::lock_guard<std::mutex> lk(mtx);
        auto it = recs.find(rec->handle);
        if (it != recs.end() && it->second == rec) recs.erase(it);
    }

    void cancelAll()
    {
        std::unordered_map<TimerHandle, QtTimerRecPtr> dead;
        {
            std::lock_guard<std::mutex> lk(mtx);
            dead.swap(recs);
        }
        for (auto& kv : dead) kv.second->cancel();
    }

    std::mutex mtx;
    std::unordered_map<TimerHandle, QtTimerRecPtr> recs;
};

using QtTimerStatePtr = std::shared_ptr<QtTimerState>;

} // namespace

// Owns the Qt-side timer ids and dispatches ticks. Everything here runs on the
// host's own thread.
class QtTimerHost : public QObject
{
public:
    QtTimerHost(Loop* owner, const QtTimerStatePtr& state)
        : owner_(owner), state_(state)
    {}

    void arm(const QtTimerRecPtr& rec)
    {
        if (rec->dead()) return;   // removed while this arm sat in the queue
        rec->qt_id = startTimer(rec->interval, Qt::PreciseTimer);
        if (rec->qt_id) {
            by_qt_[rec->qt_id] = rec;
        } else {
            rec->cancel();
            if (auto state = state_.lock()) state->retire(rec);
        }
    }

    void stop(const QtTimerRecPtr& rec)
    {
        if (!rec->qt_id) return;
        killTimer(rec->qt_id);
        by_qt_.erase(rec->qt_id);
        rec->qt_id = 0;
    }

protected:
    void timerEvent(QTimerEvent* event) override
    {
        auto it = by_qt_.find(event->timerId());
        if (it == by_qt_.end()) {
            killTimer(event->timerId());   // stray id, nothing owns it
            return;
        }
        QtTimerRecPtr rec = it->second;    // copied: stop() invalidates the iterator

        if (rec->dead()) { stop(rec); return; }

        // Qt timers always repeat; single shot is retired here — before the
        // tick, so removeTimer() called inside it is honest.
        if (!rec->repeat) {
            rec->cancel();
            stop(rec);
            if (auto state = state_.lock()) state->retire(rec);
        }

        Loop* prev = loop::current();
        loop::setCurrent(owner_);
        // Throwing through the Qt event loop is undefined behaviour.
        try {
            rec->tick();
        } catch (const std::exception& e) {
            veLogE << "qt timer tick threw:" << e.what();
        } catch (...) {
            veLogE << "qt timer tick threw";
        }
        loop::setCurrent(prev);
    }

private:
    Loop* owner_ = nullptr;
    std::weak_ptr<QtTimerState> state_;
    std::unordered_map<int, QtTimerRecPtr> by_qt_;
};

class QtTimers
{
public:
    using TimerHandle = Loop::TimerHandle;

    QtTimers(Loop* owner, QObject* context)
    {
        if (!context) return;
        host_ = new QtTimerHost(owner, state_);   // parentless, so it can be moved
        host_->moveToThread(context->thread());
    }

    ~QtTimers()
    {
        // Cancel first so queued arms and later timer events become no-ops. The
        // host stops its native timers as it dies; cross-thread QObject
        // destruction is deferred to its owning event loop.
        state_->cancelAll();
        if (!host_) return;
        if (host_->thread() == QThread::currentThread()) delete host_;
        else host_->deleteLater();
        host_ = nullptr;
    }

    TimerHandle add(uint64_t ms, bool repeat, Task tick)
    {
        if (!tick || !host_) return 0;

        auto rec = std::make_shared<QtTimerRec>();
        rec->tick     = std::move(tick);
        rec->handle   = ++seq_;
        rec->interval = static_cast<int>(ms);
        rec->repeat   = repeat;

        state_->add(rec);

        QMetaObject::invokeMethod(host_, [host = host_, rec] { host->arm(rec); },
                                  Qt::QueuedConnection);
        return rec->handle;
    }

    bool remove(TimerHandle h)
    {
        QtTimerRecPtr rec = state_->take(h);
        if (!rec) return false;
        if (rec->dead()) return false;   // a single shot already retired it
        kill(rec);
        return true;
    }

private:
    void kill(const QtTimerRecPtr& rec)
    {
        rec->cancel();
        if (!host_) return;
        QMetaObject::invokeMethod(host_, [host = host_, rec] { host->stop(rec); },
                                  Qt::QueuedConnection);
    }

    QtTimerHost* host_ = nullptr;
    QtTimerStatePtr state_ = std::make_shared<QtTimerState>();
    std::atomic<TimerHandle> seq_{0};
};

QtLoop::QtLoop(QEventLoop* loop, const std::string& name)
    : Loop(name)
    , loop_(loop)
    , timers_(std::make_unique<QtTimers>(this, loop))
{}

QtLoop::~QtLoop() = default;

void QtLoop::post(Task task)
{
    if (!task || !loop_) return;
    QMetaObject::invokeMethod(loop_, [task = std::move(task)]() mutable {
        task();
    }, Qt::QueuedConnection);
}

size_t QtLoop::processEvents()
{
    if (!loop_) return 0;
    loop_->processEvents();
    return 0;
}

Loop::TimerHandle QtLoop::addTimer(uint64_t ms, bool repeat, Task tick)
{
    return timers_->add(ms, repeat, std::move(tick));
}

bool QtLoop::removeTimer(TimerHandle handle)
{
    return timers_->remove(handle);
}

QtMainLoop::QtMainLoop(QCoreApplication* app, const std::string& name)
    : Loop(name)
    , app_(app ? app : QCoreApplication::instance())
    , timers_(std::make_unique<QtTimers>(this, app_))
{}

QtMainLoop::~QtMainLoop() = default;

void QtMainLoop::post(Task task)
{
    if (!task || !app_) return;
    QMetaObject::invokeMethod(app_, [task = std::move(task)]() mutable {
        task();
    }, Qt::QueuedConnection);
}

size_t QtMainLoop::processEvents()
{
    if (!app_) return 0;
    QCoreApplication::processEvents();
    return 0;
}

int QtMainLoop::exec()
{
    if (!app_) return 0;
    if (_running.exchange(true)) return -1;
    int code = app_->exec();
    _running.store(false);
    return code;
}

void QtMainLoop::quit(int exit_code)
{
    Loop::quit(exit_code);
    QCoreApplication::exit(exit_code);
}

Loop::TimerHandle QtMainLoop::addTimer(uint64_t ms, bool repeat, Task tick)
{
    return timers_->add(ms, repeat, std::move(tick));
}

bool QtMainLoop::removeTimer(TimerHandle handle)
{
    return timers_->remove(handle);
}

class QtModule : public Module
{
    QCoreApplication* app_ = nullptr;
    QtMainLoop* main_loop_ = nullptr;
    bool owns_app_ = false;
    int argc_ = 0;

public:
    QtModule() = default;

protected:
    void init() override
    {
        Node* cfg = node()->find("config");

        std::string app_type = "gui";
        if (cfg) {
            if (Node* atn = cfg->find("app_type")) {
                app_type = atn->getString("gui");
            }
        }

        applyEarlySettings();

        if (QCoreApplication::instance()) {
            app_ = QCoreApplication::instance();
        } else {
            auto [argc_v, argv_v] = entry::args();
            // Q(Core|Gui|Application) retains the argc reference, so the
            // backing integer must remain alive for the application's lifetime.
            argc_ = argc_v;
            if (app_type == "widgets") {
                app_ = new QApplication(argc_, argv_v);
            } else {
                app_ = new QGuiApplication(argc_, argv_v);
            }
            owns_app_ = true;
        }

        main_loop_ = new QtMainLoop(app_);
        loop::setMain(main_loop_);
    }

    void prepare() override
    {
        Node* cfg = node()->find("config");

        // Wire up cross-module log/event bridges once peers are constructed
        // (qInstallMessageHandler is process-global; imol bridges connect to
        // peers that may have been registered by other modules' init()).
        installQtMessageHandlerIfNeeded(cfg);

#ifdef VE_QT_HAS_IMOL
        bool bridge_signals = true;
        if (cfg) {
            if (Node* n = cfg->find("log/bridge_imol_log_signals")) {
                bridge_signals = n->getBool(true);
            }
        }
        if (bridge_signals) {
            QObject::connect(imol::legacy::d(QStringLiteral("ve.log.debug")), &imol::ModuleObject::changed,
                [](const QVariant& var, const QVariant&, QObject*) { qDebug() << var.toString(); });
            QObject::connect(imol::legacy::d(QStringLiteral("ve.log.info")), &imol::ModuleObject::changed,
                [](const QVariant& var, const QVariant&, QObject*) { qInfo() << var.toString(); });
            QObject::connect(imol::legacy::d(QStringLiteral("ve.log.warning")), &imol::ModuleObject::changed,
                [](const QVariant& var, const QVariant&, QObject*) { qWarning() << var.toString(); });
            QObject::connect(imol::legacy::d(QStringLiteral("ve.log.error")), &imol::ModuleObject::changed,
                [](const QVariant& var, const QVariant&, QObject*) { qCritical() << var.toString(); });
        }

        bool bridge_export = true;
        if (cfg) {
            if (Node* n = cfg->find("log/bridge_imol_log_export")) {
                bridge_export = n->getBool(true);
            }
        }
        if (bridge_export) {
            QObject::connect(imol::legacy::d(QStringLiteral("ve.log.export")), &imol::ModuleObject::changed,
                [](const QVariant&, const QVariant&, QObject*) {
                auto files_d = imol::legacy::d(QStringLiteral("ve.log.export.files"));
                files_d->clear(nullptr);
                QDir ld("log");
                for (auto fi : ld.entryInfoList(QDir::Files)) {
                    QFile f(fi.absoluteFilePath());
                    if (f.open(QIODevice::ReadOnly)) {
                        files_d->append(nullptr, fi.baseName())->set(QString::fromUtf8(f.readAll()));
                        f.close();
                    }
                }
            });
        }

        QObject::connect(imol::legacy::d(QStringLiteral("ve.rescue.record")), &imol::ModuleObject::changed,
            [](const QVariant& v, const QVariant&, QObject*) {
                static QtOperationRecorder r(nullptr);
                if (!qApp) {
                    return;
                }
                if (v.toBool()) {
                    qApp->installEventFilter(&r);
                } else {
                    qApp->removeEventFilter(&r);
                }
            });
#else
        if (cfg && qApp) {
            if (Node* n = cfg->find("rescue/record_ui_events")) {
                if (n->getBool(false)) {
                    static QtOperationRecorder r2(nullptr);
                    qApp->installEventFilter(&r2);
                }
            }
        }
#endif
    }

    void deinit() override
    {
        if (main_loop_) {
            loop::setMain(nullptr);
            delete main_loop_;
            main_loop_ = nullptr;
        }
        if (owns_app_) {
            delete app_;
        }
        app_ = nullptr;
    }
};

} // namespace ve::qt

VE_REGISTER_PRIORITY_MODULE(ve.qt, ve::qt::QtModule, 5, 1)
