#include "completions.hpp"

#include <string_view>

namespace {

const char* kBash =
    "# bash completion for ssg: source this file, or save it in bash-completion's completions dir.\n"
    "_ssg() {\n"
    "  local cur=${COMP_WORDS[COMP_CWORD]} cmds=\"build init new serve watch check list clean "
    "plugins doctor render completions\"\n"
    "  local flags=\"--config --output --base-url --drafts --check-links --incremental --target --force --port --open --verbose "
    "--quiet --color --json --fail-on-warning --no-plugins --help --version\"\n"
    "  if [ \"$COMP_CWORD\" -eq 1 ]; then\n"
    "    COMPREPLY=($(compgen -W \"$cmds $flags\" -- \"$cur\"))\n"
    "  elif [ \"${COMP_WORDS[1]}\" = completions ]; then\n"
    "    COMPREPLY=($(compgen -W \"bash zsh fish\" -- \"$cur\"))\n"
    "  elif [ \"${COMP_WORDS[1]}\" = plugins ] && [ \"$COMP_CWORD\" -eq 2 ]; then\n"
    "    COMPREPLY=($(compgen -W \"list meta\" -- \"$cur\"))\n"
    "  elif [ \"${COMP_WORDS[COMP_CWORD-1]}\" = --color ]; then\n"
    "    COMPREPLY=($(compgen -W \"auto always never\" -- \"$cur\"))\n"
    "  elif [[ \"$cur\" == -* ]]; then\n"
    "    COMPREPLY=($(compgen -W \"$flags\" -- \"$cur\"))\n"
    "  else\n"
    "    COMPREPLY=($(compgen -f -- \"$cur\"))\n"
    "  fi\n"
    "}\n"
    "complete -o filenames -F _ssg ssg\n";

const char* kZsh =
    "#compdef ssg\n"
    "# zsh completion for ssg: save as _ssg in a directory on $fpath.\n"
    "_ssg() {\n"
    "  local -a cmds flags\n"
    "  cmds=(build init new serve watch check list clean plugins doctor render completions)\n"
    "  flags=(--config --output --base-url --drafts --check-links --incremental --target --force --port --open --verbose --quiet\n"
    "         --color --json --fail-on-warning --no-plugins --help --version)\n"
    "  if (( CURRENT == 2 )); then\n"
    "    compadd -a cmds flags\n"
    "  elif [[ $words[2] == completions ]]; then\n"
    "    compadd bash zsh fish\n"
    "  elif [[ $words[2] == plugins && CURRENT -eq 3 ]]; then\n"
    "    compadd list meta\n"
    "  elif [[ $words[CURRENT-1] == --color ]]; then\n"
    "    compadd auto always never\n"
    "  elif [[ $words[CURRENT] == -* ]]; then\n"
    "    compadd -a flags\n"
    "  else\n"
    "    _files\n"
    "  fi\n"
    "}\n"
    "_ssg \"$@\"\n";

const char* kFish =
    "# fish completion for ssg: save as ~/.config/fish/completions/ssg.fish\n"
    "complete -c ssg -f\n"
    "complete -c ssg -n __fish_use_subcommand -a 'build init new serve watch check list clean "
    "plugins doctor render completions'\n"
    "complete -c ssg -n '__fish_seen_subcommand_from completions' -a 'bash zsh fish'\n"
    "complete -c ssg -n '__fish_seen_subcommand_from plugins' -a 'list meta'\n"
    "complete -c ssg -l config -r -d 'read this file instead of site.toml'\n"
    "complete -c ssg -l output -r -d 'build into this directory'\n"
    "complete -c ssg -l base-url -r -d 'override base_url'\n"
    "complete -c ssg -l drafts -d 'also build drafts'\n"
    "complete -c ssg -l check-links -d 'warn about broken local links'\n"
    "complete -c ssg -l incremental -d 'keep dist and rewrite only what changed'\n"
    "complete -c ssg -l target -x -a 'cloudflare vercel nginx' -d 'write deploy files'\n"
    "complete -c ssg -l force -d 'replace deploy files that differ'\n"
    "complete -c ssg -l port -r -d 'port for serve'\n"
    "complete -c ssg -l open -d 'open the served site in a browser'\n"
    "complete -c ssg -l verbose -d 'one line per page and file'\n"
    "complete -c ssg -l quiet -s q -d 'no summary line'\n"
    "complete -c ssg -l color -x -a 'auto always never' -d 'colour warnings'\n"
    "complete -c ssg -l json -d 'machine-readable result on stdout'\n"
    "complete -c ssg -l fail-on-warning -d 'exit 3 on warnings'\n"
    "complete -c ssg -l no-plugins -d 'do not run plugins'\n"
    "complete -c ssg -l help -s h -d 'show help'\n"
    "complete -c ssg -l version -d 'show the version'\n"
    "complete -c ssg -n '__fish_seen_subcommand_from build init new serve watch check list clean doctor "
    "render' -F\n";

}  // namespace

const char* completion_script(const char* shell) {
  const std::string_view s = shell;
  if (s == "bash") return kBash;
  if (s == "zsh") return kZsh;
  if (s == "fish") return kFish;
  return nullptr;
}
