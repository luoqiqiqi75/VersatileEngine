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
// QtTimers — QTimer-backed scheduler shared by QtLoop and QtMainLoop
// ============================================================================
//
// A QTimer must be created, started and stopped on the thread that owns it, but
// addTimer()/removeTimer() may be called from any thread. Both are therefore
// marshalled onto the context object's thread, and each timer carries a
// cancelled flag so a remove that beats the queued create still wins.
//
class QtTimers
{
public:
    using TimerHandle = Loop::TimerHandle;

    QtTimers(Loop* owner, QObject* context) : owner_(owner), context_(context) {}

    ~QtTimers() { clear(); }

    TimerHandle add(uint64_t ms, bool repeat, Task tick)
    {
        if (!tick || !context_) return 0;

        const TimerHandle h = ++seq_;
        auto rec = std::make_shared<Rec>();
        rec->tick = std::move(tick);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            recs_[h] = rec;
        }

        QMetaObject::invokeMethod(context_, [this, h, rec, ms, repeat] {
            if (rec->cancelled.load(std::memory_order_acquire)) return;

            auto* qt_timer = new QTimer(context_);
            qt_timer->setTimerType(Qt::PreciseTimer);
            qt_timer->setSingleShot(!repeat);

            QObject::connect(qt_timer, &QTimer::timeout, qt_timer, [this, h, rec, repeat, qt_timer] {
                if (rec->cancelled.load(std::memory_order_acquire)) return;
                if (!repeat) {
                    // Retire before the tick, so removeTimer() is honest inside it.
                    forget(h);
                    rec->cancelled.store(true, std::memory_order_release);
                    rec->timer.store(nullptr, std::memory_order_release);
                    qt_timer->deleteLater();
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
            });

            // Publish only after a final cancel check: remove() may have run
            // while this create sat in the event queue.
            rec->timer.store(qt_timer, std::memory_order_release);
            if (rec->cancelled.load(std::memory_order_acquire)) {
                rec->timer.store(nullptr, std::memory_order_release);
                qt_timer->deleteLater();
                return;
            }
            qt_timer->start(static_cast<int>(ms));
        }, Qt::QueuedConnection);

        return h;
    }

    bool remove(TimerHandle h)
    {
        std::shared_ptr<Rec> rec;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            auto it = recs_.find(h);
            if (it == recs_.end()) return false;
            rec = it->second;
            recs_.erase(it);
        }
        kill(rec);
        return true;
    }

    void clear()
    {
        std::unordered_map<TimerHandle, std::shared_ptr<Rec>> dead;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            dead.swap(recs_);
        }
        for (auto& kv : dead) kill(kv.second);
    }

private:
    struct Rec {
        Task tick;
        std::atomic<bool>    cancelled{false};
        std::atomic<QTimer*> timer{nullptr};   // written on the context thread, read anywhere
    };

    // Cancel first (the tick becomes a no-op immediately, on any thread), then
    // let Qt tear the QTimer down on its own thread. deleteLater() is
    // thread-safe and stops the timer as it destroys it.
    static void kill(const std::shared_ptr<Rec>& rec)
    {
        rec->cancelled.store(true, std::memory_order_release);
        if (QTimer* t = rec->timer.exchange(nullptr, std::memory_order_acq_rel)) t->deleteLater();
    }

    void forget(TimerHandle h)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        recs_.erase(h);
    }

    Loop*    owner_ = nullptr;
    QObject* context_ = nullptr;
    std::atomic<TimerHandle> seq_{0};

    std::mutex mtx_;
    std::unordered_map<TimerHandle, std::shared_ptr<Rec>> recs_;
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

bool QtLoop::isRunning() const
{
    return loop_ && loop_->isRunning();
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

bool QtMainLoop::isRunning() const
{
    return app_ != nullptr;
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
    if (_quit.exchange(false)) return _exit_code.load();   // quit() before exec()
    int code = app_->exec();
    _quit.store(false);
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
            // QApplication expects int& — copy to a local, its adjustments
            // stay local since the process argv is not touched again.
            int argc = argc_v;
            if (app_type == "widgets") {
                app_ = new QApplication(argc, argv_v);
            } else {
                app_ = new QGuiApplication(argc, argv_v);
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
