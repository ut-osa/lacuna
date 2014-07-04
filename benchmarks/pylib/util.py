import subprocess
import re

def get_git_rev(override_clean_check=False):
    if not override_clean_check and \
            subprocess.call(['git', 'diff', '--quiet']) == 1:
        # Git modifications detected
        raise Exception('Unclean tree.  Stash your changes for benchmark consistency.')
    top_rev = subprocess.Popen(['git', 'log', '--oneline', '-1'],
                               stdout=subprocess.PIPE).stdout.read()
    m = re.match('^([0-9a-f]+)', top_rev)
    if m is None:
        raise Exception('Couldn\'t parse git rev')
    return m.group(1)
