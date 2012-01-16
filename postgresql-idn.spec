%define PG_MODULE_ROOT %( pg_config --pkglibdir )
%define PG_MODULE_SHARE %( pg_config --sharedir )

Name:           postgresql-idn
Version:        0.2
Release:        1

Group:          System Environment/Base
Summary:        idn functions for PostgreSQL
License:        3-clause BSD
URL:            http://www.dyn.com/
Source0:        %{name}-%{version}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(id -u -n)

BuildRequires:  libidn-devel
Requires:  libidn

BuildRequires: postgresql-devel postgresql-libs
Requires: postgresql-libs

%description
A postgresql interface for various functions provided by libidn and libidn2.

%prep
%setup -q

%build
make build

%install
make -C sources -f Makefile install  DESTDIR=$RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(0644,root,root,0755)
%doc README.rst
%attr(0755, root, root) %PG_MODULE_ROOT/*.so
%PG_MODULE_SHARE

%changelog
