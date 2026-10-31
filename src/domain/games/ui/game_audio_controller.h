#pragma once

#include <QObject>
#include <QPointer>

class QAudioOutput;
class QMediaPlayer;
class QWidget;

namespace mtb::games::ui {

class GameAudioController final : public QObject {
public:
    static GameAudioController& instance();

    void claim(QWidget* owner, bool enabled);
    void release(QWidget* owner);
    void setEnabled(QWidget* owner, bool enabled);
    [[nodiscard]] bool isPlayingFor(const QWidget* owner) const;

private:
    explicit GameAudioController(QObject* parent = nullptr);
    void playIfAllowed();

    QMediaPlayer* player_;
    QAudioOutput* output_;
    QPointer<QWidget> owner_;
    bool enabled_{true};
};

}  // namespace mtb::games::ui
