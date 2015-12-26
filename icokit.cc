#include <iostream>
#include <list>
#include <functional>
#include <windows.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <direct.h>
#include <tchar.h>
#include "icondir.h"
using namespace std;

class disposer {
	list<function<void()>>* pcbs;
public:
	disposer() {
		pcbs = new list<function<void()>>();
	}
	~disposer() {
		while (!pcbs->empty()) {
			pcbs->back()(); 
			pcbs->pop_back();
		}
		delete pcbs;
	}
	void push(const function<void()> cb) {
		pcbs->push_back(cb);
	}
};

#define SUCCESS(x) if (!x) { throw GetLastError(); }

LPTSTR cpystr(LPCTSTR src){
	auto dest = new TCHAR[_tcslen(src) + 1];
	_tcscpy(dest, src);
	return dest;
}

class icongroup {
public:
	LPTSTR name;
	WORD   lang;
	icongroup(LPCTSTR name, WORD lang) {
		this->name = cpystr(name);
		this->lang = lang;
	}
	~icongroup() {
		delete name;
	}
};

HGLOBAL lockres(disposer* pd, HMODULE module, LPCTSTR type, LPCTSTR name, WORD lang, DWORD* psize = NULL) {
	auto fr = FindResourceEx(module, type, name, lang); 
	SUCCESS(fr);
	auto lr = LoadResource(module, fr); 
	SUCCESS(lr);
	auto kr = LockResource(lr); 
	SUCCESS(kr);
	pd->push([=] { FreeResource(kr); }); 
	if (psize != NULL)
		*psize = SizeofResource(module, fr); 
	return kr;
}

typedef function<WINBOOL(HMODULE module, LPCTSTR type, LPCTSTR name, WORD lang)> FOREACHCALLBACK;
void foreach(LPCTSTR path, FOREACHCALLBACK cb) {
	disposer d;
	auto module = LoadLibraryEx(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
	d.push([module] { FreeLibrary(module); });	
	BOOL active = TRUE;
	SUCCESS(EnumResourceNames(module, RT_GROUP_ICON, [](HMODULE module, LPCTSTR type, LPTSTR name, LONG_PTR param) {
		SUCCESS(EnumResourceLanguages(module, type, name, [](HMODULE module, LPCTSTR type, LPCTSTR name, WORD lang, LONG_PTR param) {
			return (*(FOREACHCALLBACK*)param)(module, type, name, lang);
		}, param));
		return TRUE;
	}, (LONG_PTR)&cb))
}
void extract(disposer* pd, HMODULE module, LPCTSTR type, LPCTSTR name, WORD lang, LPCTSTR path) {
	auto pgrpdir = ((GRPICONDIR*)lockres(pd, module, type, name, lang));
	auto count = pgrpdir->idCount;
	auto pf = _tfopen(path, "wb");
	pd->push([=] { fclose(pf); });
	auto headersize = sizeof(WORD) * 3 + sizeof(ICONDIRENTRY) * count;
	auto pdir = (ICONDIR*)malloc(headersize);
	pd->push([pdir] { free(pdir); });
	memcpy(pdir, pgrpdir, sizeof(WORD) * 3);
	fwrite(pdir, 1, headersize, pf);
	for (auto c = 0; c < count; c++) {
		auto id = pgrpdir->idEntries[c].nID;
		TCHAR entryname[16];
		_stprintf(entryname, "#%d", id); 
		DWORD size;
		auto pico = lockres(pd, module, RT_ICON, entryname, lang, &size);
		memcpy(&pdir->idEntries[c], &pgrpdir->idEntries[c], sizeof(GRPICONDIRENTRY) - sizeof(WORD));
		fpos_t cur;
		fgetpos(pf, &cur);
		pdir->idEntries[c].dwImageOffset = cur;
		fwrite(pico, 1, size, pf);
	}
	rewind(pf);
	fwrite(pdir, 1, headersize, pf);
}

#define HAS_FLAG(x, f) ((x & f) == f)
struct tpinfo {
public:
	bool success;
	bool dir;
};

bool testpath(LPCTSTR path, tpinfo* info){
	struct _tstat stat;
	info->success = _tstat(path, &stat) == 0;
	info->dir     = HAS_FLAG(stat.st_mode,  _S_IFDIR);
	return info->success;
}
void mkdirsafe(LPCTSTR dir) {
	tpinfo tp;
	testpath(dir, &tp);
	if (tp.success && !tp.dir)
		throw "path is not directory";
	if (!tp.success) 
		_tmkdir(dir);
}

int main() {
	foreach("test/target.exe", [](HMODULE module, LPCTSTR type, LPCTSTR name, WORD lang){
		disposer d;
		TCHAR dir[32]; 
		_stprintf(dir, "test/%d", lang);
		mkdirsafe(dir);
		TCHAR path[_MAX_PATH];
		_tmakepath(path, NULL, dir, name, ".ico"); 
		cout << "Extracting: " << name << "(" << lang << ")" << endl;
		extract(&d, module, type, name, lang, path);
		return TRUE;
	});
}

