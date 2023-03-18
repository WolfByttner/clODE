
PYFILES=$(shell find clode/python -name "*.py")
PYTESTFILES=$(shell find test -name "*.py")

PYTHON ?= python

venv:
	$(PYTHON) -m venv venv

install:
	$(PYTHON) -m pip install --upgrade pip && \
		$(PYTHON) -m pip install -r requirements.txt

install_clode:
	$(PYTHON) -m pip install .

format:
	isort $(PYFILES) $(PYTESTFILES) && \
			black $(PYFILES) $(PYTESTFILES)

test: install install_clode
	$(PYTHON) -m pytest $(PYTESTFILES)

run: install
	. venv/bin/activate && PYTHONPATH=$(PYTHONPATH) python main.py

lint: install
	vulture $(PYFILES) $(PYTESTFILES) && \
		$(PYTHON) -m pylint $(PYFILES) $(PYTESTFILES) && \
		mypy $(PYFILES) $(PYTESTFILES)