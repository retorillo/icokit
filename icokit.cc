#include <iostream>
#include <list>
#include <map>
#include <functional>
#include <windows.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <direct.h>
#include <tchar.h>
#include <string>
#include <sstream>
#include <stdexcept>
#include "icondir.h"
using namespace std;

#if UNICODE
	typedef wstring       STDSTR;
	typedef wstringstream STDSTRSTR;
#else
	typedef string        STDSTR;
	typedef stringstream  STDSTRSTR;
#endif


class disposer {
	list<function<void()>> cbs;
public:
	~disposer() {
		while (!cbs.empty()) {
			cbs.back()(); 
			cbs.pop_back();
		}
	}
	void push(const function<void()> cb) {
		cbs.push_back(cb);
	}
};

class cfmt {
	STDSTRSTR stream; 
public:
	cfmt() {}
	~cfmt() {}
	template <typename T>
	cfmt & operator << (const T & value) {
		stream << value;
		return *this;
	}
	operator STDSTR () { return stream.str();  } 
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

HGLOBAL lockres(disposer& d, HMODULE module, LPCTSTR type, LPCTSTR name, WORD lang, DWORD* psize = NULL) {
	auto fr = FindResourceEx(module, type, name, lang); 
	SUCCESS(fr);
	auto lr = LoadResource(module, fr); 
	SUCCESS(lr);
	auto kr = LockResource(lr); 
	SUCCESS(kr);
	d.push([=] { FreeResource(kr); }); 
	if (psize != NULL)
		*psize = SizeofResource(module, fr); 
	return kr;
}

typedef function<WINBOOL(HMODULE module, LPCTSTR type, LPCTSTR name, WORD lang)> FOREACHCALLBACK;
void foreach(LPCTSTR path, FOREACHCALLBACK cb) throw (const runtime_error&) {
	disposer d;
	try {
		auto module = LoadLibraryEx(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
		SUCCESS(module);
		d.push([module] { FreeLibrary(module); });	
		BOOL active = TRUE;
		SUCCESS(EnumResourceNames(module, RT_GROUP_ICON, [](HMODULE module, LPCTSTR type, LPTSTR name, LONG_PTR param) {
			SUCCESS(EnumResourceLanguages(module, type, name, [](HMODULE module, LPCTSTR type, LPCTSTR name, WORD lang, LONG_PTR param) {
				return (*(FOREACHCALLBACK*)param)(module, type, name, lang);
			}, param));
			return TRUE;
		}, (LONG_PTR)&cb)) 
	}
	catch (DWORD u) {
		//TODO: format
		throw runtime_error("Check LASTERROR");
	}
}
void extract(disposer& d, HMODULE module, LPCTSTR type, LPCTSTR name, WORD lang, LPCTSTR path) {
	auto pgrpdir = ((GRPICONDIR*)lockres(d, module, type, name, lang));
	auto count = pgrpdir->idCount;
	auto pf = _tfopen(path, "wb");
	d.push([=] { fclose(pf); });
	auto headersize = sizeof(WORD) * 3 + sizeof(ICONDIRENTRY) * count;
	auto pdir = (ICONDIR*)malloc(headersize);
	d.push([pdir] { free(pdir); });
	memcpy(pdir, pgrpdir, sizeof(WORD) * 3);
	fwrite(pdir, 1, headersize, pf);
	for (auto c = 0; c < count; c++) {
		auto id = pgrpdir->idEntries[c].nID;
		TCHAR entryname[16];
		_stprintf(entryname, "#%d", id); 
		DWORD size;
		auto pico = lockres(d, module, RT_ICON, entryname, lang, &size);
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

template <class T> class switcher{
	map<T, function<void(void*)>> list;
public:
	function<void(void*)> fallback;
	switcher<T> _case(T str, function<void(void*)> callback) {
		list[str] = callback;
		return *this;
	}
	void _exec(T str, void* param = NULL){
		auto callback = list[str];
		if (callback) callback(param);
		else if (fallback) fallback(param);
	}
};


struct STRCMP
{
	bool operator() (LPCTSTR a, LPCTSTR b)
	{
		return lstrcmpi(a, b) < 0;
	}
};
typedef map<LPCTSTR, LPCTSTR, STRCMP> OPTMAP; 
class argreader {
	OPTMAP optalias;
	list<LPCTSTR> args;
public:
	argreader(int argc, const TCHAR** argv) {
		for (auto c = 0; c < argc; c++) {
			args.push_back(argv[c]);
		}
	}
	~argreader() {
	}
	LPCTSTR next() throw (const runtime_error&) {
		if (args.empty()) {
			throw runtime_error("ARGUMENT NOT FOUND");
		}
		auto front = args.front(); 
		args.pop_front(); 
		return front;
	}
	
	argreader defalias(LPCTSTR alias, LPCTSTR name){
		optalias[alias] = name;
		return *this;
	}
	void readopt(OPTMAP& map) throw (const runtime_error&) {
		LPCTSTR key = nullptr;
		auto reg = [&map, &key] (LPCTSTR value) throw (const runtime_error&) {
			if (key == nullptr && !map.empty()) 
				throw runtime_error(cfmt() << "Unexpected literal: " << value);
			if (key != nullptr && map[key] != nullptr)
			 	throw runtime_error(cfmt() << "Duplicated argument: " << key);
			map[key] = value;
			key = nullptr;
		};
		while(!args.empty()) {
			auto n = next();
			if (n[0] == '-') {
				if (key != nullptr) 
					reg("");
				key = optalias[n];
				key = key ? key : n;
			}
			else {
				reg(n);
			}
		}
	}
};

int main(int argc, TCHAR** argv) {
	try
	{
		argreader reader(argc, (const TCHAR**)argv);
		reader.defalias("--output", "-o");
		OPTMAP opt;
		auto exe = reader.next(); // startup binary path
		auto cmd = reader.next();
		reader.readopt(opt);
		
		if (lstrcmp(cmd, "e") == 0 || lstrcmp(cmd, "extract") == 0) {
			auto source = opt[nullptr];
			auto output = opt["-o"];
			foreach(source, [=](HMODULE module, LPCTSTR type, LPCTSTR name, WORD lang){
				disposer d;
				TCHAR dir[256]; 
				// TODO: Path.Combine
				_stprintf(dir, "%s\\%d", output, lang);
				mkdirsafe(dir);
				TCHAR path[_MAX_PATH];
				_tmakepath(path, NULL, dir, name, ".ico"); 
				cout << "Extracting: " << name << "(" << lang << ")" << endl;
				extract(d, module, type, name, lang, path);
				return TRUE;
			});
		}
	}
	catch (const runtime_error& e){
		cerr << e.what() << endl;
	}
}



