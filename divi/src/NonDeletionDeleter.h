#ifndef NON_DELETION_DELETER_H
#define NON_DELETION_DELETER_H
 template <typename T>
 struct NonDeletionDeleter {
  void operator()(T* r) {}
 };
#endif// NON_DELETION_DELETER_H