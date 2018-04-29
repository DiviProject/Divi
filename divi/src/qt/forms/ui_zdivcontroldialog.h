// /********************************************************************************
// ** Form generated from reading UI file 'zdivcontroldialog.ui'
// **
// ** Created by: Qt User Interface Compiler version 5.10.1
// **
// ** WARNING! All changes made in this file will be lost when recompiling UI file!
// ********************************************************************************/

// #ifndef UI_ZDIVCONTROLDIALOG_H
// #define UI_ZDIVCONTROLDIALOG_H

// #include <QtCore/QVariant>
// #include <QtWidgets/QAction>
// #include <QtWidgets/QApplication>
// #include <QtWidgets/QButtonGroup>
// #include <QtWidgets/QDialog>
// #include <QtWidgets/QDialogButtonBox>
// #include <QtWidgets/QFormLayout>
// #include <QtWidgets/QGridLayout>
// #include <QtWidgets/QHeaderView>
// #include <QtWidgets/QLabel>
// #include <QtWidgets/QPushButton>
// #include <QtWidgets/QTreeWidget>
// #include <QtWidgets/QVBoxLayout>

// QT_BEGIN_NAMESPACE

// class Ui_ZDivControlDialog
// {
// public:
//     QGridLayout *gridLayout;
//     QFormLayout *formLayout;
//     QLabel *labelQuantity;
//     QLabel *labelQuantity_int;
//     QLabel *labelZDiv;
//     QLabel *labelZDiv_int;
//     QPushButton *pushButtonAll;
//     QVBoxLayout *verticalLayout;
//     QTreeWidget *treeWidget;
//     QDialogButtonBox *buttonBox;

//     void setupUi(QDialog *ZDivControlDialog)
//     {
//         if (ZDivControlDialog->objectName().isEmpty())
//             ZDivControlDialog->setObjectName(QStringLiteral("ZDivControlDialog"));
//         ZDivControlDialog->resize(681, 384);
//         ZDivControlDialog->setMinimumSize(QSize(681, 384));
//         gridLayout = new QGridLayout(ZDivControlDialog);
//         gridLayout->setObjectName(QStringLiteral("gridLayout"));
//         formLayout = new QFormLayout();
//         formLayout->setObjectName(QStringLiteral("formLayout"));
//         formLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
//         labelQuantity = new QLabel(ZDivControlDialog);
//         labelQuantity->setObjectName(QStringLiteral("labelQuantity"));

//         formLayout->setWidget(0, QFormLayout::LabelRole, labelQuantity);

//         labelQuantity_int = new QLabel(ZDivControlDialog);
//         labelQuantity_int->setObjectName(QStringLiteral("labelQuantity_int"));

//         formLayout->setWidget(0, QFormLayout::FieldRole, labelQuantity_int);

//         labelZDiv = new QLabel(ZDivControlDialog);
//         labelZDiv->setObjectName(QStringLiteral("labelZDiv"));

//         formLayout->setWidget(1, QFormLayout::LabelRole, labelZDiv);

//         labelZDiv_int = new QLabel(ZDivControlDialog);
//         labelZDiv_int->setObjectName(QStringLiteral("labelZDiv_int"));

//         formLayout->setWidget(1, QFormLayout::FieldRole, labelZDiv_int);

//         pushButtonAll = new QPushButton(ZDivControlDialog);
//         pushButtonAll->setObjectName(QStringLiteral("pushButtonAll"));

//         formLayout->setWidget(2, QFormLayout::LabelRole, pushButtonAll);


//         gridLayout->addLayout(formLayout, 0, 0, 1, 1);

//         verticalLayout = new QVBoxLayout();
//         verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
//         treeWidget = new QTreeWidget(ZDivControlDialog);
//         QTreeWidgetItem *__qtreewidgetitem = new QTreeWidgetItem();
//         __qtreewidgetitem->setText(3, QStringLiteral("Confirmations"));
//         __qtreewidgetitem->setText(2, QStringLiteral("zDiv Public ID"));
//         __qtreewidgetitem->setText(1, QStringLiteral("Denomination"));
//         __qtreewidgetitem->setText(0, QStringLiteral("Select"));
//         treeWidget->setHeaderItem(__qtreewidgetitem);
//         treeWidget->setObjectName(QStringLiteral("treeWidget"));
//         treeWidget->setMinimumSize(QSize(0, 250));
//         treeWidget->setAlternatingRowColors(true);
//         treeWidget->setSortingEnabled(true);
//         treeWidget->setColumnCount(5);
//         treeWidget->header()->setDefaultSectionSize(100);

//         verticalLayout->addWidget(treeWidget);

//         buttonBox = new QDialogButtonBox(ZDivControlDialog);
//         buttonBox->setObjectName(QStringLiteral("buttonBox"));
//         buttonBox->setOrientation(Qt::Horizontal);
//         buttonBox->setStandardButtons(QDialogButtonBox::Ok);

//         verticalLayout->addWidget(buttonBox);


//         gridLayout->addLayout(verticalLayout, 1, 0, 1, 1);


//         retranslateUi(ZDivControlDialog);
//         QObject::connect(buttonBox, SIGNAL(accepted()), ZDivControlDialog, SLOT(accept()));
//         QObject::connect(buttonBox, SIGNAL(rejected()), ZDivControlDialog, SLOT(reject()));

//         QMetaObject::connectSlotsByName(ZDivControlDialog);
//     } // setupUi

//     void retranslateUi(QDialog *ZDivControlDialog)
//     {
//         ZDivControlDialog->setWindowTitle(QApplication::translate("ZDivControlDialog", "Select zDiv to Spend", nullptr));
//         labelQuantity->setText(QApplication::translate("ZDivControlDialog", "Quantity", nullptr));
//         labelQuantity_int->setText(QApplication::translate("ZDivControlDialog", "0", nullptr));
//         labelZDiv->setText(QApplication::translate("ZDivControlDialog", "zDiv", nullptr));
//         labelZDiv_int->setText(QApplication::translate("ZDivControlDialog", "0", nullptr));
//         pushButtonAll->setText(QApplication::translate("ZDivControlDialog", "Select/Deselect All", nullptr));
//         QTreeWidgetItem *___qtreewidgetitem = treeWidget->headerItem();
//         ___qtreewidgetitem->setText(4, QApplication::translate("ZDivControlDialog", "Is Spendable", nullptr));
//     } // retranslateUi

// };

// namespace Ui {
//     class ZDivControlDialog: public Ui_ZDivControlDialog {};
// } // namespace Ui

// QT_END_NAMESPACE

// #endif // UI_ZDIVCONTROLDIALOG_H
