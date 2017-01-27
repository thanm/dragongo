//===-- namegen.h - decls for 'NameGen' class --======================//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines NameGen class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVMGOFRONTEND_NAMEGEN_H
#define LLVMGOFRONTEND_NAMEGEN_H

class NameGen {
 public:
  NameGen() { }

  // Tells namegen to choose its own version number for the created name
  static constexpr unsigned ChooseVer = 0xffffffff;

  // For creating useful type, inst and block names.
  std::string namegen(const std::string &tag, unsigned expl = ChooseVer) {
    auto it = nametags_.find(tag);
    unsigned count = 0;
    if (it != nametags_.end())
      count = it->second + 1;
    if (expl != ChooseVer)
      count = expl;
    std::stringstream ss;
    ss << tag << "." << count;
    if (expl == ChooseVer)
      nametags_[tag] = count;
    return ss.str();
  }

  NameGen *nameTags() {
    return const_cast<NameGen*>(this);
  }

 private:
  // Key is tag (ex: "add") and val is counter to uniquify.
  std::unordered_map<std::string, unsigned> nametags_;
};



#endif // LLVMGOFRONTEND_TYPEMANAGER_H
