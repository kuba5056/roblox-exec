// rbx-exec-ui — gtk4 frontend. Editor, console, attach/eject.
// Talks to shim64 over a unix socket which winepath translates
// to \\.\pipe\rbx-exec inside the sober prefix.

mod ipc;
mod sober;

use anyhow::Result;
use gtk4::{glib, prelude::*, Application, ApplicationWindow};
use gtk4::{Box as GBox, Button, HeaderBar, Label, Orientation, Paned,
           ScrolledWindow, TextView};
use std::cell::RefCell;
use std::rc::Rc;

const APP_ID: &str = "org.olivia.rbxExec";

struct AppState {
    client: RefCell<Option<ipc::Client>>,
    log_view: TextView,
}

fn main() -> glib::ExitCode {
    let app = Application::builder().application_id(APP_ID).build();
    app.connect_activate(build_ui);
    app.run()
}

fn build_ui(app: &Application) {
    let win = ApplicationWindow::builder()
        .application(app)
        .title("rbx-exec — ollie's bench")
        .default_width(1180)
        .default_height(760)
        .build();

    let header = HeaderBar::new();
    let btn_attach = Button::with_label("attach");
    let btn_run    = Button::with_label("run");
    let btn_eject  = Button::with_label("eject");
    header.pack_start(&btn_attach);
    header.pack_start(&btn_run);
    header.pack_end(&btn_eject);
    win.set_titlebar(Some(&header));

    let editor = TextView::builder().monospace(true).build();
    editor.buffer().set_text("-- rbx-exec: paste luau here\nprint('hello from ollie')\n");

    let editor_scroll = ScrolledWindow::builder()
        .child(&editor).vexpand(true).hexpand(true).build();

    let log_view = TextView::builder().monospace(true).editable(false).build();
    let log_scroll = ScrolledWindow::builder()
        .child(&log_view).vexpand(true).hexpand(true).build();

    let paned = Paned::builder()
        .orientation(Orientation::Vertical)
        .position(520)
        .start_child(&editor_scroll)
        .end_child(&log_scroll)
        .build();

    let status = Label::new(Some("idle — sober not attached"));
    let root = GBox::new(Orientation::Vertical, 4);
    root.append(&paned);
    root.append(&status);
    win.set_child(Some(&root));

    let state = Rc::new(AppState {
        client: RefCell::new(None),
        log_view: log_view.clone(),
    });

    {
        let state = state.clone();
        let status = status.clone();
        btn_attach.connect_clicked(move |_| {
            match sober::launch_and_inject() {
                Ok(sock) => match ipc::Client::connect(&sock) {
                    Ok(c)  => { *state.client.borrow_mut() = Some(c);
                                status.set_text("attached"); }
                    Err(e) => status.set_text(&format!("ipc: {e}")),
                },
                Err(e) => status.set_text(&format!("launch: {e}")),
            }
        });
    }

    {
        let state = state.clone();
        let status = status.clone();
        let buf = editor.buffer();
        btn_run.connect_clicked(move |_| {
            let (s, e) = buf.bounds();
            let src = buf.text(&s, &e, false).to_string();
            let mut b = state.client.borrow_mut();
            if let Some(c) = b.as_mut() {
                match c.exec_source(&src) {
                    Ok(reply) => append_log(&state.log_view, &reply),
                    Err(e)    => status.set_text(&format!("run: {e}")),
                }
            } else {
                status.set_text("attach first");
            }
        });
    }

    {
        let state = state.clone();
        let status = status.clone();
        btn_eject.connect_clicked(move |_| {
            if let Some(mut c) = state.client.borrow_mut().take() {
                let _ = c.eject();
                status.set_text("ejected");
            }
        });
    }

    win.present();
}

fn append_log(view: &TextView, line: &str) {
    let buf = view.buffer();
    let mut end = buf.end_iter();
    buf.insert(&mut end, line);
    buf.insert(&mut end, "\n");
}
