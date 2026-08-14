#pragma once

#include <QObject>

namespace irautox {

class ShortcutSync final : public QObject {
public:
    explicit ShortcutSync(QObject *parent = nullptr);

private:
    void sync();
};

} // namespace irautox
