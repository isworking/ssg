#include "build.hpp"
#include "document.hpp"
#include "markdown.hpp"
#include "util.hpp"

#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static bool path_is_inside(const fs::path& dir, const fs::path& p) {
  const auto d = dir.lexically_normal();
  const auto n = p.lexically_normal();
  auto dit = d.begin();
  auto nit = n.begin();
  for (; dit != d.end() && nit != n.end(); ++dit, ++nit) {
    if (*dit == ".") continue;
    if (*nit != *dit) return false;
  }
  return dit == d.end();
}

static bool is_markdown(const fs::path& p) { return p.extension() == ".md"; }

static fs::path publish_rel(const fs::path& rel, const std::set<fs::path>& md_rels) {
  if (rel.extension() != ".md" || rel.filename() == "index.md") return rel;
  const fs::path as_index = (rel.parent_path() / rel.stem() / "index.md").lexically_normal();
  if (md_rels.count(as_index)) return rel;
  return as_index;
}

void build_site(const SiteConfig& cfg) {
  if (!fs::is_directory(cfg.content_dir)) {
    throw std::runtime_error("content dir is not a directory: " + cfg.content_dir.string());
  }
  if (!fs::exists(cfg.skeleton_path)) {
    throw std::runtime_error("missing HTML skeleton: " + cfg.skeleton_path.string());
  }

  const std::string skeleton = read_file(cfg.skeleton_path);
  fs::create_directories(cfg.dist_dir);

  std::vector<fs::path> files;
  for (auto it = fs::recursive_directory_iterator(cfg.content_dir);
       it != fs::recursive_directory_iterator(); ++it) {
    const auto p = it->path();
    if (it->is_directory()) {
      if (path_is_inside(cfg.dist_dir, p) || path_is_inside(cfg.templates_dir, p)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (!it->is_regular_file()) continue;
    if (path_is_inside(cfg.dist_dir, p) || path_is_inside(cfg.templates_dir, p)) continue;
    files.push_back(p);
  }

  std::set<fs::path> md_rels;
  for (const auto& src : files) {
    if (is_markdown(src)) md_rels.insert(fs::relative(src, cfg.content_dir).lexically_normal());
  }

  for (const auto& src : files) {
    const auto rel = publish_rel(fs::relative(src, cfg.content_dir).lexically_normal(), md_rels);
    const auto dest = cfg.dist_dir / rel;

    if (!is_markdown(src)) {
      fs::create_directories(dest.parent_path());
      fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
      continue;
    }

    ParsedDoc doc = parse_markdown_source(read_file(src));
    RenderedDoc rendered = render_extended(cfg, src, doc);
    std::string final_md = std::move(rendered.markdown);
    if (!final_md.empty() && final_md.back() != '\n') final_md.push_back('\n');

    write_file(dest, final_md);

    const std::string html_body = markdown_to_html(final_md);
    std::string html = replace_ci_tag_inner(skeleton, "<main", "</main>", html_body);
    auto title = rendered.vars.find("title");
    if (title != rendered.vars.end()) {
      html = replace_ci_tag_inner(html, "<title", "</title>", html_escape(title->second), false, false);
    }
    auto html_path = dest;
    html_path.replace_extension(".html");
    write_file(html_path, html);

    auto txt_path = dest;
    txt_path.replace_extension(".txt");
    write_file(txt_path, markdown_to_text(final_md));
  }
}
