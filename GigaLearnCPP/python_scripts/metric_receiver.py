import site
import sys
import json
import os

wandb_run = None

def _wandb_logged_in():
	# `wandb login` stores the key in ~/.netrc under the server it logged in to. That used to be api.wandb.ai and is now
	# api.forge.coreweave.com (W&B moved to CoreWeave), so accept any W&B / CoreWeave host. WANDB_API_KEY also counts.
	if os.environ.get("WANDB_API_KEY"):
		return True
	try:
		import netrc
		for host, auth in netrc.netrc().hosts.items():
			if ("wandb" in host or "coreweave" in host) and auth and auth[2]:
				return True
	except Exception:
		pass
	return False

# Takes in the python executable path, the three wandb init strings, and optionally the current run ID
# Returns the ID of the run (either newly created or resumed)
def init(py_exec_path, project, group, name, id = None):

	global wandb_run
	
	# Fix the path of our interpreter so wandb doesn't run RLGym_PPO instead of Python
	# Very strange fix for a very strange problem
	sys.executable = py_exec_path
	
	try:
		site_packages_dir = os.path.join(os.path.join(os.path.dirname(py_exec_path), "Lib"), "site-packages")
		sys.path.append(site_packages_dir)
		site.addsitedir(site_packages_dir)
		import wandb
	except Exception as e:
		raise Exception(f"""
			FAILED to import wandb! Make sure GigaLearnCPP isn't using the wrong Python installation.
			This installation's site packages: {site.getsitepackages()}
			Exception: {repr(e)}"""
		)
	
	# W&B is ON by default. Without a login it falls back to an offline run (nothing is lost; upload
	# later with `wandb sync wandb/offline-run-*`). Log in once with `wandb login` to sync live.
	# An explicit WANDB_MODE (online/offline/disabled) always wins.
	if not os.environ.get("WANDB_MODE"):
		if _wandb_logged_in():
			os.environ["WANDB_MODE"] = "online"
		else:
			os.environ["WANDB_MODE"] = "offline"
			print("wandb: not logged in -> logging OFFLINE. Run `wandb login` once to sync online.")
	print("wandb: mode = " + os.environ["WANDB_MODE"])
	print("Calling wandb.init()...")
	if not (id is None) and len(id) > 0:
		wandb_run = wandb.init(project = project, group = group, name = name, id = id, resume = "allow")
	else:
		wandb_run = wandb.init(project = project, group = group, name = name)
	return wandb_run.id

def add_metrics(metrics):
	global wandb_run
	wandb_run.log(metrics)