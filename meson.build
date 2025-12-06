project('webserver-gtk', 'c',
  version: '1.5',
  default_options: [
    'buildtype=release',
    'strip=true',
    'warning_level=3'
  ]
)

sources = [
    'main.c'
]


gtk_dep = dependency('gtk4')

executable('webserver-gtk',
           sources: sources,
           dependencies: [gtk_dep],
           install: true)
