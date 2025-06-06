
project = 'eAlloc'
extensions = ['breathe']
html_theme = 'sphinx_rtd_theme'

html_baseurl = 'https://fahara02.github.io/eAlloc/'
html_theme_options = {
    'canonical_url': 'https://fahara02.github.io/eAlloc/',
}
html_static_path = ['_static']
html_extra_path = ['.']
breathe_projects = {
    'eAlloc': '../doxygen/xml'
}
breathe_default_project = 'eAlloc'
master_doc = 'index'
