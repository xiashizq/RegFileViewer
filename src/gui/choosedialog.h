#pragma once

#include "core/interface.h"
#include "core/fileparser.h"

#include <QDialog>
#include <QList>

class QTableWidget;

namespace rfv {

class ChooseDialog : public QDialog
{
    Q_OBJECT

public:
    ChooseDialog(const QList<InterfaceDefinition>& candidates,
                 const FileProbe& probe, QWidget* parent = nullptr);

    InterfaceDefinition selected() const;

private:
    QList<InterfaceDefinition> m_candidates;
    QTableWidget* m_table = nullptr;
};

} // namespace rfv
